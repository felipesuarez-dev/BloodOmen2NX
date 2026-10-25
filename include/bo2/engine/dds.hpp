#pragma once

#include <cstdint>
#include <vector>

#include "bo2/engine/image.hpp"

namespace bo2::engine {

Image decode_dds(const std::vector<std::uint8_t>& data);

}  // namespace bo2::engine
