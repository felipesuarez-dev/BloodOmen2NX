#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bo2::engine {

struct BigEntry {
    std::string type;
    std::string name;
    std::uint16_t language = 0;
    std::size_t offset = 0;
    std::size_t size = 0;
    std::vector<std::uint16_t> symbols;
};

class BigArchive {
public:
    static BigArchive load(const std::string& path);
    static BigArchive parse(std::vector<std::uint8_t> data);

    const std::vector<BigEntry>& entries() const { return entries_; }
    const std::vector<std::string>& names() const { return names_; }
    const std::vector<std::uint8_t>& bytes() const { return data_; }
    std::vector<std::uint8_t> extract(const BigEntry& entry) const;
    const BigEntry* find(const std::string& type, const std::string& name) const;

private:
    std::vector<std::uint8_t> data_;
    std::vector<BigEntry> entries_;
    std::vector<std::string> names_;
};

bool iequals(const std::string& left, const std::string& right);
bool contains_ci(const std::string& haystack, const std::string& needle);

}  // namespace bo2::engine
