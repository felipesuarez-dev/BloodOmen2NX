#include "bo2/engine/big_archive.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "bo2/common/reader.hpp"

namespace bo2::engine {

namespace {

constexpr std::size_t kHeaderSize = 0x24;
constexpr std::size_t kEntryHeader = 44;
constexpr std::size_t kSymbolsOffset = 0x1C;

std::string type_at(const std::vector<std::uint8_t>& data, std::size_t offset) {
    std::string type;
    for (std::size_t index = 0; index < 8; ++index) {
        const std::uint8_t value = data[offset + index];
        if (value == 0) {
            break;
        }
        type.push_back(static_cast<char>(value));
    }
    return type;
}

bool all_zero(const std::vector<std::uint8_t>& data, std::size_t offset) {
    for (std::size_t index = offset; index < data.size(); ++index) {
        if (data[index] != 0) {
            return false;
        }
    }
    return true;
}

char lower_ascii(char value) {
    const auto byte = static_cast<unsigned char>(value);
    if (byte >= 'A' && byte <= 'Z') {
        return static_cast<char>(byte - 'A' + 'a');
    }
    return value;
}

}  // namespace

bool iequals(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (lower_ascii(left[index]) != lower_ascii(right[index])) {
            return false;
        }
    }
    return true;
}

bool contains_ci(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > haystack.size()) {
        return false;
    }
    std::string lowered_haystack = haystack;
    std::string lowered_needle = needle;
    std::transform(lowered_haystack.begin(), lowered_haystack.end(), lowered_haystack.begin(), lower_ascii);
    std::transform(lowered_needle.begin(), lowered_needle.end(), lowered_needle.begin(), lower_ascii);
    return lowered_haystack.find(lowered_needle) != std::string::npos;
}

BigArchive BigArchive::load(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open " + path);
    }
    std::vector<std::uint8_t> data{
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    return parse(std::move(data));
}

BigArchive BigArchive::parse(std::vector<std::uint8_t> data) {
    BigArchive archive;
    archive.data_ = std::move(data);
    const common::Reader reader(archive.data_);
    if (archive.data_.size() < kHeaderSize || reader.cstr(0, 8) != "goefile") {
        throw common::ParseError("not a goefile container");
    }

    const std::uint32_t directory_length = reader.u32(0x18);
    const std::uint32_t name_count = reader.u32(0x20);
    const std::size_t entries_start = 0x10 + directory_length;
    if (entries_start < kHeaderSize || entries_start > archive.data_.size()) {
        throw common::ParseError("bad directory length");
    }

    std::vector<std::string> names;
    names.reserve(name_count);
    std::size_t position = kHeaderSize;
    for (std::uint32_t index = 0; index < name_count; ++index) {
        std::string name = reader.cstr(position, entries_start);
        position += name.size() + 1;
        names.push_back(std::move(name));
    }
    if ((position & 1U) != 0U) {
        ++position;
    }
    if (position > entries_start) {
        throw common::ParseError("name table overruns the directory");
    }

    position = entries_start;
    while (position < archive.data_.size()) {
        if (archive.data_.size() - position < kEntryHeader) {
            if (!all_zero(archive.data_, position)) {
                throw common::ParseError("truncated entry");
            }
            break;
        }
        const std::string type = type_at(archive.data_, position);
        if (type.empty()) {
            if (!all_zero(archive.data_, position)) {
                throw common::ParseError("bad entry type");
            }
            break;
        }
        const std::size_t entry_length = reader.u32(position + 8);
        const std::size_t entry_end = position + entry_length;
        if (entry_length < kEntryHeader || entry_end > archive.data_.size()) {
            throw common::ParseError("bad entry length");
        }
        const std::uint16_t name_index = reader.u16(position + 16);
        const std::uint16_t language = reader.u16(position + 18);
        const std::size_t symbols_size = reader.u32(position + kSymbolsOffset);
        const std::size_t data_length_at = position + 20 + symbols_size;
        if (data_length_at + 4 > entry_end) {
            throw common::ParseError("bad symbol list");
        }
        const std::size_t data_length = reader.u32(data_length_at);
        const std::size_t padded = (data_length + 3U) & ~std::size_t{3U};
        if (padded > entry_length) {
            throw common::ParseError("bad data length");
        }
        const std::size_t data_offset = entry_end - padded;
        if (data_offset < data_length_at + 4 || data_offset > entry_end) {
            throw common::ParseError("bad data range");
        }

        BigEntry entry;
        entry.type = type;
        entry.name = name_index < names.size() ? names[name_index] : "#" + std::to_string(name_index);
        entry.language = language;
        entry.offset = data_offset;
        entry.size = data_length;
        archive.entries_.push_back(std::move(entry));
        position = entry_end;
    }
    return archive;
}

std::vector<std::uint8_t> BigArchive::extract(const BigEntry& entry) const {
    if (entry.offset > data_.size() || entry.size > data_.size() - entry.offset) {
        throw common::ParseError("entry data past the end of the container");
    }
    return std::vector<std::uint8_t>(
        data_.begin() + static_cast<std::ptrdiff_t>(entry.offset),
        data_.begin() + static_cast<std::ptrdiff_t>(entry.offset + entry.size));
}

const BigEntry* BigArchive::find(const std::string& type, const std::string& name) const {
    for (const BigEntry& entry : entries_) {
        if (iequals(entry.type, type) && iequals(entry.name, name)) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace bo2::engine
