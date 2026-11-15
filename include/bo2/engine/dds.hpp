#pragma once

#include <cstdint>
#include <vector>

#include "bo2/engine/image.hpp"

namespace bo2::engine {

Image decode_dds(const std::vector<std::uint8_t>& data);

// FUN_0056f320: the blend class of a texture, read from the flags and the
// FourCC of its DDS header. Throws ParseError on a payload shorter than
// the header fields or without the `DDS ` mark.
struct TextureBlend {
    bool blends = false;
    bool writes_depth = false;
};

TextureBlend texture_blend(const std::vector<std::uint8_t>& dds);

}  // namespace bo2::engine
