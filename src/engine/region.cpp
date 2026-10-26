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

RegionEntity parse_entity(const common::Reader& reader, std::size_t offset, std::size_t end) {
    if (offset + 4 > end) {
        throw common::ParseError("entity is truncated");
    }
    RegionEntity entity;
    entity.flags = reader.u32(offset);
    entity.name = reader.cstr(offset + 4, end);
    offset = offset + 4 + entity.name.size() + 1;
    entity.model = reader.cstr(offset, end);
    offset = align4(offset + entity.model.size() + 1);
    if (offset + 12 > end) {
        throw common::ParseError("entity position is truncated");
    }
    entity.position = {read_float(reader, offset), read_float(reader, offset + 4),
                       read_float(reader, offset + 8)};
    return entity;
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

}  // namespace bo2::engine
