#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bo2/engine/math.hpp"

namespace bo2::engine {

struct LevelTriangle {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;
    std::uint32_t material = 0;
    std::uint32_t model = 0;
    std::int32_t texture = -1;
};

struct LevelGeometry {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> texcoords;
    std::vector<std::string> texture_names;
    std::vector<LevelTriangle> triangles;
    Vec3 minimum{0.0f, 0.0f, 0.0f};
    Vec3 maximum{0.0f, 0.0f, 0.0f};
    std::uint32_t model_count = 0;
    std::uint32_t skipped_models = 0;
};

LevelGeometry parse_cell_group(const std::vector<std::uint8_t>& data);
LevelGeometry parse_cell_group(const std::vector<std::uint8_t>& data,
                               const std::vector<std::uint16_t>& symbols,
                               const std::vector<std::string>& names);

}  // namespace bo2::engine
