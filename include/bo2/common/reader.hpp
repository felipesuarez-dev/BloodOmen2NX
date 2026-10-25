#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace bo2::common {

class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
    explicit Reader(const std::vector<std::uint8_t>& data) : Reader(data.data(), data.size()) {}

    std::size_t size() const { return size_; }

    void require(std::size_t offset, std::size_t length) const {
        if (offset > size_ || length > size_ - offset) {
            throw ParseError("read past the end of the buffer");
        }
    }

    std::uint8_t u8(std::size_t offset) const {
        require(offset, 1);
        return data_[offset];
    }

    std::uint16_t u16(std::size_t offset) const {
        require(offset, 2);
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(data_[offset]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(data_[offset + 1]) << 8));
    }

    std::uint32_t u32(std::size_t offset) const {
        require(offset, 4);
        return static_cast<std::uint32_t>(data_[offset]) |
               (static_cast<std::uint32_t>(data_[offset + 1]) << 8) |
               (static_cast<std::uint32_t>(data_[offset + 2]) << 16) |
               (static_cast<std::uint32_t>(data_[offset + 3]) << 24);
    }

    std::uint64_t u64(std::size_t offset) const {
        require(offset, 8);
        std::uint64_t value = 0;
        for (int index = 0; index < 8; ++index) {
            value |= static_cast<std::uint64_t>(data_[offset + static_cast<std::size_t>(index)])
                     << (8 * index);
        }
        return value;
    }

    std::string cstr(std::size_t offset, std::size_t limit) const {
        if (limit > size_) {
            limit = size_;
        }
        for (std::size_t index = offset; index < limit; ++index) {
            if (data_[index] == 0) {
                return std::string(reinterpret_cast<const char*>(data_ + offset), index - offset);
            }
        }
        throw ParseError("unterminated string");
    }

    std::vector<std::uint8_t> bytes(std::size_t offset, std::size_t length) const {
        require(offset, length);
        return std::vector<std::uint8_t>(data_ + offset, data_ + offset + length);
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
};

}  // namespace bo2::common
