#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "bo2/common/reader.hpp"

namespace bo2::testing {

// Every prefix length of `bytes` that makes `parse` throw something other than
// a ParseError (a defect of the reader, not a rejected input).
template <typename Parse>
std::vector<std::size_t> bad_prefixes(const std::vector<std::uint8_t>& bytes, Parse parse) {
    std::vector<std::size_t> bad;
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        const std::vector<std::uint8_t> prefix(
            bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length));
        try {
            parse(prefix);
        } catch (const bo2::common::ParseError&) {
        } catch (...) {
            bad.push_back(length);
        }
    }
    return bad;
}

}  // namespace bo2::testing
