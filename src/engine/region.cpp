#include "bo2/engine/region.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

#include "bo2/common/reader.hpp"

namespace bo2::engine {

namespace {

std::size_t align4(std::size_t value) {
    return (value + 3) & ~static_cast<std::size_t>(3);
}

float read_float(const common::Reader& reader, std::size_t offset) {
    const std::uint32_t bits = reader.u32(offset);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

std::string type_name(const common::Reader& reader, std::size_t offset) {
    std::string name;
    for (std::size_t index = 0; index < 8; ++index) {
        const std::uint8_t value = reader.u8(offset + index);
        if (value == 0) {
            break;
        }
        name.push_back(static_cast<char>(value));
    }
    return name;
}

std::vector<std::uint8_t> region_payload(const std::vector<std::uint8_t>& data) {
    if (data.size() < 8 ||
        std::memcmp(data.data(), "goefile\0", 8) != 0) {
        return data;
    }
    if (data.size() < 0x24) {
        throw common::ParseError("region container is shorter than its header");
    }
    const common::Reader reader(data);
    std::size_t position = 0x10 + reader.u32(0x18);
    while (position + 12 <= data.size()) {
        const std::string name = type_name(reader, position);
        const std::uint32_t size = reader.u32(position + 8);
        if (size < 12 || position + size > data.size()) {
            throw common::ParseError("bad nested region entry");
        }
        if (name == "region") {
            return std::vector<std::uint8_t>(
                data.begin() + static_cast<std::ptrdiff_t>(position + 12),
                data.begin() + static_cast<std::ptrdiff_t>(position + size));
        }
        position += size;
    }
    throw common::ParseError("nested region container has no region entry");
}

// An entity record between `begin` and `end` (docs/FORMATS/region.md,
// "Region entity table"); every padding to 4 counts from `begin`.
RegionEntity parse_entity(const common::Reader& reader, std::size_t begin, std::size_t end) {
    if (begin + 4 > end) {
        throw common::ParseError("entity is truncated");
    }
    const auto padded = [begin](std::size_t offset) { return begin + align4(offset - begin); };
    RegionEntity entity;
    entity.flags = reader.u32(begin);
    entity.name = reader.cstr(begin + 4, end);
    std::size_t offset = begin + 4 + entity.name.size() + 1;
    entity.model = reader.cstr(offset, end);
    offset = padded(offset + entity.model.size() + 1);
    if (offset + 12 > end) {
        throw common::ParseError("entity position is truncated");
    }
    entity.position = {read_float(reader, offset), read_float(reader, offset + 4),
                       read_float(reader, offset + 8)};
    // The properties up to the end of the record: the 0 bytes before a key
    // are skipped; a key (cstr padded to 4), its type, then a cstr padded to
    // 4 for type 8 or a u32 for the other types.
    offset += 12;
    while (offset < end) {
        if (reader.u8(offset) == 0) {
            ++offset;
            continue;
        }
        RegionProperty property;
        property.key = reader.cstr(offset, end);
        offset = padded(offset + property.key.size() + 1);
        if (offset + 4 > end) {
            throw common::ParseError("entity property " + property.key + " is truncated");
        }
        property.type = reader.u32(offset);
        offset += 4;
        if (property.type == 8) {
            property.text = reader.cstr(offset, end);
            offset = padded(offset + property.text.size() + 1);
        } else if (offset + 4 <= end) {
            property.number = reader.u32(offset);
            offset += 4;
        } else {
            throw common::ParseError("entity property " + property.key + " has no value");
        }
        entity.properties.push_back(std::move(property));
    }
    return entity;
}

// The `packages` section between `begin` and `end` (docs/FORMATS/bpackage.md,
// "Region packages"): u32 4, u32 package count, u16 1, u16 0xffff, the
// package name (cstr padded to 4), u32 size (the bytes from the name to the
// end of the section), u32 0, then the package up to the end of the section.
ScriptPackage read_packages_section(const std::vector<std::uint8_t>& payload, std::size_t begin,
                                    std::size_t end, const std::vector<std::uint16_t>& symbols,
                                    const std::vector<std::string>& names) {
    const common::Reader reader(payload);
    if (begin + 12 > end) {
        throw common::ParseError("packages section is truncated");
    }
    if (reader.u32(begin + 4) != 1) {
        throw common::ParseError("packages section does not hold one package");
    }
    const std::size_t name_offset = begin + 12;
    const std::string name = reader.cstr(name_offset, end);
    const std::size_t offset = name_offset + align4(name.size() + 1);
    if (offset + 8 > end) {
        throw common::ParseError("packages section is truncated");
    }
    if (name_offset + reader.u32(offset) != end) {
        throw common::ParseError("region package size does not reach the end of its section");
    }
    if (reader.u32(offset + 4) != 0) {
        throw common::ParseError("region package header is not followed by 0");
    }
    return parse_script_package(reader.bytes(offset + 8, end - offset - 8), symbols, names);
}

}  // namespace

std::vector<RegionEntityTable> parse_region_entities(
    const std::vector<std::uint8_t>& data) {
    const std::vector<std::uint8_t> payload = region_payload(data);
    const common::Reader reader(payload);
    if (payload.size() < 8) {
        throw common::ParseError("region payload is truncated");
    }
    std::vector<RegionEntityTable> tables;
    std::size_t position = 8;
    while (position + 12 <= payload.size()) {
        const std::string name = type_name(reader, position);
        if (name.empty()) {
            break;
        }
        const std::uint32_t size = reader.u32(position + 8);
        if (size < 12 || position + size > payload.size()) {
            throw common::ParseError("bad region section size");
        }
        if (name == "entities") {
            const std::size_t section_end = position + size;
            if (position + 20 > section_end) {
                throw common::ParseError("entities section is truncated");
            }
            RegionEntityTable table;
            table.package = reader.u32(position + 12);
            table.marker = reader.u32(position + 16);
            std::size_t chunk = position + 20;
            while (chunk + 12 <= section_end) {
                const std::string chunk_name = type_name(reader, chunk);
                if (chunk_name != "entity") {
                    break;
                }
                const std::uint32_t chunk_size = reader.u32(chunk + 8);
                if (chunk_size < 12 || chunk + chunk_size > section_end) {
                    throw common::ParseError("bad entity chunk size");
                }
                table.entities.push_back(
                    parse_entity(reader, chunk + 12, chunk + chunk_size));
                chunk += chunk_size;
            }
            tables.push_back(std::move(table));
        }
        position += size;
    }
    return tables;
}

ScriptPackage parse_region_script_package(const std::vector<std::uint8_t>& data,
                                          const std::vector<std::uint16_t>& symbols,
                                          const std::vector<std::string>& names) {
    const std::vector<std::uint8_t> payload = region_payload(data);
    const common::Reader reader(payload);
    std::size_t position = 8;
    while (position + 12 <= payload.size()) {
        const std::string name = type_name(reader, position);
        if (name.empty()) {
            break;
        }
        const std::uint32_t size = reader.u32(position + 8);
        if (size < 12 || position + size > payload.size()) {
            throw common::ParseError("bad region section size");
        }
        if (name == "packages") {
            return read_packages_section(payload, position + 12, position + size, symbols, names);
        }
        position += size;
    }
    throw common::ParseError("region has no packages section");
}

// FUN_007828c0.
std::string reserved_script(const RegionEntity& entity) {
    for (const RegionProperty& property : entity.properties) {
        if (property.key == "reserved.script") {
            return property.text;
        }
    }
    return {};
}

// FUN_00783900: isalpha 0x7a529f and isalnum 0x7a53c2 of the C locale, the
// trimmed set " \t\r\n" at 0x90ab0c, tolower 0x7a4456.
std::string region_package_name(const std::string& region_name) {
    const auto letter = [](char value) { return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z'); };
    const auto digit = [](char value) { return value >= '0' && value <= '9'; };
    std::size_t first = 0;
    while (first < region_name.size() && !letter(region_name[first])) {
        ++first;
    }
    std::size_t last = region_name.size();
    while (last > first && std::string(" \t\r\n").find(region_name[last - 1]) != std::string::npos) {
        --last;
    }
    std::string name;
    for (std::size_t index = first; index < last; ++index) {
        const char value = region_name[index];
        if (!letter(value) && !digit(value)) {
            name.push_back('_');
        } else if (value >= 'A' && value <= 'Z') {
            name.push_back(static_cast<char>(value - 'A' + 'a'));
        } else {
            name.push_back(value);
        }
    }
    return name;
}

}  // namespace bo2::engine
