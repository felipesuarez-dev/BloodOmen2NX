#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "bo2/common/reader.hpp"
#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/region.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

void put32(std::vector<std::uint8_t>& data, std::uint32_t value) {
    data.push_back(static_cast<std::uint8_t>(value & 0xFF));
    data.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    data.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    data.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void putf(std::vector<std::uint8_t>& data, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    put32(data, bits);
}

void append(std::vector<std::uint8_t>& data, const std::string& value) {
    data.insert(data.end(), value.begin(), value.end());
}

std::vector<std::uint8_t> entity_chunk(const std::string& name, const std::string& model,
                                       const std::vector<std::uint8_t>& properties = {}) {
    std::vector<std::uint8_t> record;
    put32(record, 0);
    append(record, name);
    record.push_back(0);
    append(record, model);
    record.push_back(0);
    while (record.size() % 4 != 0) {
        record.push_back(0);
    }
    putf(record, 1.0f);
    putf(record, 2.0f);
    putf(record, 3.0f);
    record.insert(record.end(), properties.begin(), properties.end());
    std::vector<std::uint8_t> chunk;
    append(chunk, std::string("entity\0\0", 8));
    put32(chunk, static_cast<std::uint32_t>(12 + record.size()));
    chunk.insert(chunk.end(), record.begin(), record.end());
    return chunk;
}

std::vector<std::uint8_t> region_payload(const std::vector<std::uint8_t>& properties = {}) {
    std::vector<std::uint8_t> payload;
    put32(payload, 1);
    put32(payload, 3);
    const std::vector<std::uint8_t> chunk = entity_chunk("dagger001", "dagger", properties);
    std::vector<std::uint8_t> section;
    put32(section, 0);
    put32(section, 9);
    section.insert(section.end(), chunk.begin(), chunk.end());
    append(payload, "entities");
    put32(payload, static_cast<std::uint32_t>(12 + section.size()));
    payload.insert(payload.end(), section.begin(), section.end());
    return payload;
}

std::vector<std::uint8_t> nested_region(const std::vector<std::uint8_t>& payload) {
    const std::string names = "region\0";
    std::vector<std::uint8_t> directory;
    append(directory, std::string("symlist\0", 8));
    put32(directory, 0);
    put32(directory, 0);
    put32(directory, 1);
    append(directory, names);
    while (directory.size() % 2 != 0) {
        directory.push_back(0);
    }
    std::vector<std::uint8_t> out;
    append(out, std::string("goefile\0", 8));
    put32(out, 0);
    put32(out, 1);
    const std::size_t directory_start = out.size();
    out.insert(out.end(), directory.begin(), directory.end());
    const std::uint32_t directory_size =
        static_cast<std::uint32_t>(out.size() - directory_start);
    std::memcpy(out.data() + 0x18, &directory_size, sizeof directory_size);
    append(out, std::string("region\0\0", 8));
    put32(out, static_cast<std::uint32_t>(12 + payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

// A package with one import, `creatures`, and no classes
// (docs/FORMATS/bpackage.md, "Package").
std::vector<std::uint8_t> minimal_package() {
    return {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
}

// The `packages` section of docs/FORMATS/bpackage.md, "Region packages".
std::vector<std::uint8_t> packages_section(std::uint32_t count, std::int32_t size_delta) {
    const std::vector<std::uint8_t> package = minimal_package();
    std::vector<std::uint8_t> section;
    put32(section, 4);
    put32(section, count);
    put32(section, 0xFFFF0001U);
    append(section, std::string("default\0", 8));
    // The size counts from the name: name, size, 0 and the package.
    put32(section, static_cast<std::uint32_t>(static_cast<std::int32_t>(16 + package.size()) + size_delta));
    put32(section, 0);
    section.insert(section.end(), package.begin(), package.end());
    std::vector<std::uint8_t> out;
    append(out, std::string("packages", 8));
    put32(out, static_cast<std::uint32_t>(12 + section.size()));
    out.insert(out.end(), section.begin(), section.end());
    return out;
}

std::vector<std::uint8_t> region_with_packages(std::uint32_t count, std::int32_t size_delta) {
    std::vector<std::uint8_t> payload;
    put32(payload, 1);
    put32(payload, 3);
    const std::vector<std::uint8_t> packages = packages_section(count, size_delta);
    payload.insert(payload.end(), packages.begin(), packages.end());
    append(payload, "entities");
    put32(payload, 20);
    put32(payload, 0);
    put32(payload, 9);
    return payload;
}

bool package_throws(const std::vector<std::uint8_t>& data) {
    try {
        static_cast<void>(bo2::engine::parse_region_script_package(data, {0}, {"creatures"}));
    } catch (const bo2::common::ParseError&) {
        return true;
    }
    return false;
}

void test_region_script_package() {
    const bo2::engine::ScriptPackage package = bo2::engine::parse_region_script_package(
        nested_region(region_with_packages(1, 0)), {0}, {"creatures"});
    CHECK(package.version == 1);
    CHECK(package.imports.size() == 1);
    CHECK(package.imports[0] == "creatures");
    CHECK(package.classes.empty());
    CHECK(!package_throws(region_with_packages(1, 0)));
    CHECK(package_throws(region_payload()));             // no packages section
    CHECK(package_throws(region_with_packages(2, 0)));   // two packages
    CHECK(package_throws(region_with_packages(1, 4)));   // size past the section
    CHECK(package_throws(region_with_packages(1, -4)));  // size short of the section
}

void test_direct_payload() {
    const auto tables = bo2::engine::parse_region_entities(region_payload());
    CHECK(tables.size() == 1);
    CHECK(tables[0].entities.size() == 1);
    CHECK(tables[0].entities[0].name == "dagger001");
    CHECK(tables[0].entities[0].model == "dagger");
    CHECK(tables[0].entities[0].position.x == 1.0f);
}

void test_nested_container() {
    const auto tables =
        bo2::engine::parse_region_entities(nested_region(region_payload()));
    CHECK(tables.size() == 1);
    CHECK(tables[0].entities.size() == 1);
    CHECK(tables[0].entities[0].model == "dagger");
}

// A key or a text: the string, its 0 and the padding to 4.
void put_padded(std::vector<std::uint8_t>& data, const std::string& value) {
    append(data, value);
    data.push_back(0);
    while (data.size() % 4 != 0) {
        data.push_back(0);
    }
}

// The properties of an entity (docs/FORMATS/region.md): a text of type 8,
// loose 0 bytes, then a number of type 3; a property cut before its value
// throws.
void test_entity_properties() {
    std::vector<std::uint8_t> properties;
    put_padded(properties, "reserved.script");
    put32(properties, 8);
    put_padded(properties, "door001");
    properties.insert(properties.end(), {0, 0, 0, 0});
    put_padded(properties, "health");
    put32(properties, 3);
    put32(properties, 25);
    const auto tables = bo2::engine::parse_region_entities(region_payload(properties));
    CHECK(tables.size() == 1);
    CHECK(tables[0].entities.size() == 1);
    const auto& found = tables[0].entities[0].properties;
    CHECK(found.size() == 2);
    if (found.size() == 2) {
        CHECK(found[0].key == "reserved.script");
        CHECK(found[0].type == 8);
        CHECK(found[0].text == "door001");
        CHECK(found[1].key == "health");
        CHECK(found[1].type == 3);
        CHECK(found[1].number == 25);
        CHECK(found[1].text.empty());
    }
    std::vector<std::uint8_t> cut;
    put_padded(cut, "health");
    put32(cut, 3);
    bool threw = false;
    try {
        static_cast<void>(bo2::engine::parse_region_entities(region_payload(cut)));
    } catch (const bo2::common::ParseError&) {
        threw = true;
    }
    CHECK(threw);
}

// FUN_007828c0.
void test_reserved_script() {
    bo2::engine::RegionEntity entity;
    CHECK(bo2::engine::reserved_script(entity).empty());
    entity.properties.push_back({"health", 3, 25, {}});
    CHECK(bo2::engine::reserved_script(entity).empty());
    entity.properties.push_back({"reserved.script", 8, 0, "door001"});
    CHECK(bo2::engine::reserved_script(entity) == "door001");
}

// FUN_00783900.
void test_region_package_name() {
    CHECK(bo2::engine::region_package_name("slums/sl01") == "slums_sl01");
    CHECK(bo2::engine::region_package_name("bossrooms/charmvampirearena/cvarena") ==
          "bossrooms_charmvampirearena_cvarena");
    CHECK(bo2::engine::region_package_name("eternal prison/ep02") == "eternal_prison_ep02");
    CHECK(bo2::engine::region_package_name("ui/optionscreen_01") == "ui_optionscreen_01");
    CHECK(bo2::engine::region_package_name("  9a/B c \t") == "a_b_c");
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("cannot open " + path.string());
    }
    const std::streamsize size = stream.tellg();
    stream.seekg(0);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// The entities of the three slums containers that name the class of their
// script object (measured with tools/bo2fmt/region.py): 78, 66 and 94, all
// a class name alone.
void test_scripted_entities_of_slums(const std::filesystem::path& root) {
    const std::filesystem::path levels = root / "media/gogames/bo2/resources/levels/slums";
    const std::vector<std::pair<std::string, std::size_t>> expected = {
        {"sl01", 78}, {"sl02", 66}, {"sl_music", 94}};
    for (const auto& [name, count] : expected) {
        const bo2::engine::BigArchive archive = bo2::engine::BigArchive::parse(read_file(levels / (name + ".big")));
        std::size_t scripted = 0;
        std::size_t regions = 0;
        for (const bo2::engine::BigEntry& entry : archive.entries()) {
            if (entry.type != "region") {
                continue;
            }
            ++regions;
            CHECK(entry.name == "slums/" + name);
            CHECK(bo2::engine::region_package_name(entry.name) == "slums_" + name);
            for (const auto& table : bo2::engine::parse_region_entities(archive.extract(entry))) {
                for (const auto& entity : table.entities) {
                    const std::string script = bo2::engine::reserved_script(entity);
                    scripted += script.empty() ? 0 : 1;
                    CHECK(script.find('/') == std::string::npos);
                }
            }
        }
        CHECK(regions == 1);
        if (scripted != count) {
            std::cerr << name << ": " << scripted << " scripted entities\n";
        }
        CHECK(scripted == count);
    }
}

}  // namespace

int main() {
    test_direct_payload();
    test_nested_container();
    test_region_script_package();
    test_entity_properties();
    test_reserved_script();
    test_region_package_name();
    if (const char* root = std::getenv("BO2NX_ASSETS_DIR")) {
        test_scripted_entities_of_slums(root);
    } else {
        std::cout << "region asset checks: skipped (BO2NX_ASSETS_DIR not set)\n";
    }
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "region tests: ok\n";
    return 0;
}
