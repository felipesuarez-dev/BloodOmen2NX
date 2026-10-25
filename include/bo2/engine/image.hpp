#pragma once

#include <cstdint>
#include <vector>

namespace bo2::engine {

struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;

    bool empty() const { return width == 0 || height == 0 || rgba.empty(); }
};

}  // namespace bo2::engine
