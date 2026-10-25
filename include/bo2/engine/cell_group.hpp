#pragma once

#include <array>
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

struct MaterialControl {
    std::uint32_t value = 0;
    float weight = 0.0f;
    std::uint16_t arg0 = 0;
    std::uint16_t arg1 = 0;
};

struct MaterialLayer {
    std::array<Vec3, 3> colors{};
    std::array<float, 3> alphas{1.0f, 1.0f, 1.0f};
    std::array<MaterialControl, 3> controls{};
    bool has_colors = false;
    bool has_controls = false;
};

struct CellAttributeTable {
    std::string name;
    std::uint32_t size = 0;
    std::vector<std::uint32_t> values;
};

struct CellPortal {
    std::uint32_t version = 0;
    std::uint16_t cell_a = 0;
    std::uint16_t cell_b = 0;
    std::vector<Vec3> vertices;
    Vec3 plane{0.0f, 0.0f, 0.0f};
    std::int32_t index = -1;
};

struct LevelCell {
    std::uint32_t version = 0;
    std::uint16_t symbol = 0;
    std::uint32_t flags = 0;
    Vec3 minimum{0.0f, 0.0f, 0.0f};
    Vec3 maximum{0.0f, 0.0f, 0.0f};
    std::vector<CellAttributeTable> attributes;
    std::vector<std::uint32_t> terrains;
    std::vector<CellPortal> portals;
    std::uint32_t dbgmodel_size = 0;
};

struct CellGrid {
    std::vector<LevelCell> cells;
};

struct BspNode {
    bool leaf = false;
    Vec3 plane{0.0f, 0.0f, 0.0f};
    float distance = 0.0f;
    std::uint16_t value = 0;
    std::int32_t left = -1;
    std::int32_t right = -1;
};

struct CellBsp {
    std::uint32_t version = 0;
    std::uint32_t leaf_count = 0;
    std::uint32_t depth = 0;
    std::vector<BspNode> nodes;
};

struct LevelGeometry {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> texcoords;
    std::vector<std::string> texture_names;
    std::vector<LevelTriangle> triangles;
    std::vector<MaterialLayer> materials;
    std::vector<CellAttributeTable> level_attributes;
    CellGrid cell_grid;
    CellBsp cell_bsp;
    Vec3 minimum{0.0f, 0.0f, 0.0f};
    Vec3 maximum{0.0f, 0.0f, 0.0f};
    std::uint32_t model_count = 0;
    std::uint32_t skipped_models = 0;
    bool has_cell_grid = false;
    bool has_cell_bsp = false;
    bool has_level_attributes = false;
};

LevelGeometry parse_cell_group(const std::vector<std::uint8_t>& data);
LevelGeometry parse_cell_group(const std::vector<std::uint8_t>& data,
                               const std::vector<std::uint16_t>& symbols,
                               const std::vector<std::string>& names);

}  // namespace bo2::engine
