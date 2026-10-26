#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bo2/engine/math.hpp"

namespace bo2::engine {

struct RegionEntity {
    std::uint32_t flags = 0;
    std::string name;
    std::string model;
    Vec3 position{0.0f, 0.0f, 0.0f};
};

struct RegionEntityTable {
    std::uint32_t package = 0;
    std::uint32_t marker = 0;
    std::vector<RegionEntity> entities;
};

std::vector<RegionEntityTable> parse_region_entities(const std::vector<std::uint8_t>& data);

}  // namespace bo2::engine
