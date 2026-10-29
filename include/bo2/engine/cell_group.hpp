#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "bo2/engine/image.hpp"
#include "bo2/engine/math.hpp"

namespace bo2::engine {

struct LevelTriangle {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;
    std::uint32_t material = 0;
    std::uint32_t model = 0;
    std::uint32_t instance = 0xFFFFFFFFU;
    std::int32_t texture = -1;
    std::int32_t lightmap = -1;
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

enum class MaterialMode { Off, Diffuse, Layers, Layers2x };

struct LevelLightMap {
    std::string name;
    std::string group;
    std::uint32_t group_vertex_count = 0;
    Image image;
    // The group's per-vertex colours (RGB of the RGBA f32 array after its
    // last entry), shared by every entry of the group; null when the file
    // has none. Stage 0 of the original modulates the texture with them
    // (D3D lighting is off).
    std::shared_ptr<const std::vector<Vec3>> group_colors;
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
    std::string name;
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

struct ModelRange {
    std::uint32_t first_vertex = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t first_triangle = 0;
    std::uint32_t triangle_count = 0;
    bool present = false;
};

struct LevelCollision {
    std::uint32_t model = 0;
    std::string name;
    std::vector<std::uint8_t> data;
};

struct ModelSkeleton {
    std::uint32_t model = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> bones;
    std::vector<float> weights;
    std::vector<std::uint16_t> joints;
    std::vector<std::uint8_t> bind_pose;
};

struct LevelLight {
    std::uint32_t version = 0;
    std::uint32_t handle = 0;
    Vec3 position{0.0f, 0.0f, 0.0f};
    std::array<float, 4> color{};
    std::array<float, 5> params{};
    std::uint32_t type = 0;
    std::uint32_t flags = 0;
    std::vector<std::uint32_t> indices;
};

struct LevelMarker {
    std::uint32_t version = 0;
    std::uint32_t id_a = 0;
    std::uint32_t id_b = 0;
    std::uint32_t symbol_a = 0;
    std::uint32_t symbol_b = 0;
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 normal{0.0f, 0.0f, 0.0f};
    std::uint32_t flag = 0;
    std::uint32_t value = 0;
    float number = 0.0f;
};

struct LevelInstance {
    std::uint32_t version = 0;
    std::uint32_t model = 0;
    // The instance's object name and its external model reference, symbols of
    // the cell group's list. FUN_00682c50 reads them as the EditTerrain name
    // and its external reference; the Terrain built from it (FUN_005cf670)
    // looks its lighting up in the level `.slm` by the lower-cased name.
    std::uint16_t name_symbol = 0;
    std::uint16_t external_symbol = 0;
    std::string name;      // resolved name; empty without a symbol table
    std::string external;  // resolved external reference; empty for a local model
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f};
    std::uint32_t flags = 0;
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
    std::vector<Vec2> lightmap_texcoords;
    // Per-vertex colours from the level .slm (assign_model_lightmaps); empty
    // when no instance has a lit group. Unlit vertices are white.
    std::vector<Vec3> vertex_colors;
    std::vector<std::string> texture_names;
    std::vector<Image> lightmaps;
    std::vector<Image> model_lightmaps;
    std::vector<LevelTriangle> triangles;
    std::vector<MaterialLayer> materials;
    std::vector<CellAttributeTable> level_attributes;
    std::vector<ModelRange> model_ranges;
    std::vector<LevelCollision> collisions;
    std::vector<ModelSkeleton> skeletons;
    std::vector<LevelInstance> instances;
    // First placed vertex of each instance (place_instances); UINT32_MAX for
    // an instance that was skipped.
    std::vector<std::uint32_t> instance_vertex_base;
    std::vector<LevelLight> lights;
    std::vector<LevelMarker> markers;
    CellGrid cell_grid;
    CellBsp cell_bsp;
    Vec3 minimum{0.0f, 0.0f, 0.0f};
    Vec3 maximum{0.0f, 0.0f, 0.0f};
    std::uint32_t model_count = 0;
    std::uint32_t skipped_models = 0;
    bool has_cell_grid = false;
    bool has_cell_bsp = false;
    bool has_level_attributes = false;
    bool has_instances = false;
    bool has_lights = false;
    bool has_markers = false;
    std::uint32_t placed_instances = 0;
    std::uint32_t skipped_instances = 0;
};

LevelGeometry parse_cell_group(const std::vector<std::uint8_t>& data);
LevelGeometry parse_cell_group(const std::vector<std::uint8_t>& data,
                               const std::vector<std::uint16_t>& symbols,
                               const std::vector<std::string>& names);
LevelGeometry parse_model_asset(const std::vector<std::uint8_t>& data,
                                const std::vector<std::uint16_t>& symbols,
                                const std::vector<std::string>& names);

Vec3 material_tint(const LevelGeometry& geometry, const LevelTriangle& triangle,
                   MaterialMode mode);
Vec3 material_color(const LevelGeometry& geometry, const LevelTriangle& triangle);
std::vector<LevelLightMap> parse_lightmaps(const std::vector<std::uint8_t>& data);
void assign_model_lightmaps(LevelGeometry& geometry,
                            const std::vector<LevelLightMap>& lightmaps);
void assign_named_model_lightmaps(LevelGeometry& geometry,
                                  const std::vector<LevelLightMap>& lightmaps,
                                  const std::string& model_name);

}  // namespace bo2::engine
