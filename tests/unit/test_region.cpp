#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

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

std::vector<std::uint8_t> entity_chunk(const std::string& name, const std::string& model) {
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
    std::vector<std::uint8_t> chunk;
    append(chunk, std::string("entity\0\0", 8));
    put32(chunk, static_cast<std::uint32_t>(12 + record.size()));
    chunk.insert(chunk.end(), record.begin(), record.end());
    return chunk;
}

std::vector<std::uint8_t> region_payload() {
    std::vector<std::uint8_t> payload;
    put32(payload, 1);
    put32(payload, 3);
    const std::vector<std::uint8_t> chunk = entity_chunk("dagger001", "dagger");
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

}  // namespace

int main() {
    test_direct_payload();
    test_nested_container();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "region tests: ok\n";
    return 0;
}
