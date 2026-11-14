#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "bo2/engine/anim_reel.hpp"
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

// One local-space collision box. The `collmodc` payload stores the hull as a
// vertex/edge list, so these only appear as the per-face and per-model bounds.
struct LevelCollisionBox {
    Vec3 minimum{0.0f, 0.0f, 0.0f};
    Vec3 maximum{0.0f, 0.0f, 0.0f};
};

// One solid sphere of a `collmodc` payload. The sphere block follows the
// triangle index block and stores 24-byte records: the centre (three floats),
// the radius, the bone index and a flag word.
struct CollisionSphere {
    Vec3 center{0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
    std::uint32_t bone = 0;
    std::uint32_t flags = 0;
};

// One triangle of a `collmodc` hull, as the pair test unpacks its record
// (FUN_0062dfc0). A record is one dword, three 9-bit vertex indices:
// a = bits 18..26, b = bits 9..17, c = bits 0..8. With bit 31 set it is two
// dwords with 16-bit indices: a = the low word of the first, b and c the two
// words of the second. The normal is (b - a) x (c - a).
struct CollisionTriangle {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;
    std::uint8_t attribute = 0;  // the top byte of the record
    // Bits 29..30 of the record. The narrow phase FUN_005ddc90 reads it.
    std::uint8_t category = 0;
};

// One convex face of a `collmodc` hull. The payload stores the face as the two
// corners of its local AABB plus a slice of the model's global triangle list.
struct CollisionFace {
    LevelCollisionBox bounds;
    std::uint32_t adjacent[2] = {0, 0};
    std::uint32_t first_triangle = 0;
    std::uint32_t triangle_count = 0;
    bool has_triangles = false;
};

// A decoded `collmodc` payload. The vertex block is a list of 6-byte points
// (three i16); the faces' AABB corners are points of the same list, so the
// model bounds and the face bounds come from those pairs. The triangle list
// follows, then the solid spheres and the face records.
struct CollisionModel {
    std::vector<Vec3> vertices;
    std::vector<CollisionTriangle> triangles;
    std::vector<CollisionFace> faces;
    std::vector<CollisionSphere> spheres;
    LevelCollisionBox bounds;
    bool valid = false;
};

// One `modlskel`/`skel` bone record. FUN_0067bdd0 reads 104 bytes per bone:
// name, state mask/value, bone and matrix indices, the collision prim and
// vertex ranges, the channel mask and rotation order, and the pivot, rest,
// joint, rotation and scale vectors. The old `count + pairs + 96 bytes`
// layout was a guess; the records are 104 bytes and start at offset 4.
struct SkeletonBone {
    std::uint32_t name_symbol = 0;
    std::uint32_t state_mask = 0;
    std::uint32_t state_value = 0;
    std::uint32_t bone_index = 0;
    std::uint32_t current_matrix = 0;
    std::uint32_t parent_matrix = 0;
    std::uint32_t first_collision_prim = 0;
    std::uint32_t collision_prim_count = 0;
    std::uint32_t first_vertex = 0;
    std::uint32_t vertex_count = 0;
    std::uint16_t channel_mask = 0;
    std::uint16_t rotation_order = 0;
    Vec3 pivot{0.0f, 0.0f, 0.0f};
    Vec3 rest_orientation{0.0f, 0.0f, 0.0f};
    Vec3 joint_orientation{0.0f, 0.0f, 0.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct ModelSkeleton {
    std::uint32_t model = 0;
    std::vector<SkeletonBone> bones;
    std::vector<float> weights;
    std::vector<std::uint16_t> joints;
};

// The slice of a LevelGeometry that an animated model occupies once the host
// appends it to the level (the player). Its vertices are rewritten every
// frame, so the slice is not level collision geometry. Empty by default.
struct ActorRange {
    std::uint32_t first_vertex = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t first_triangle = 0;
    std::uint32_t triangle_count = 0;
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

// One object of a cell group (FUN_006889a0 reads the record). `model` is the
// index of its model in `cellmdls` and `clip` the index of its clip in
// `cellanim`, each -1 for none: a camera has a clip and no model. `name` is
// the object and `entity_class` its `EntityClass`, empty for a plain child
// model. The meaning of the two indices is read from the data: in `ui/hud`
// and `sl01` they are in range and every clip is named exactly once.
struct LevelMarker {
    std::uint32_t version = 0;
    std::int32_t model = -1;
    std::int32_t clip = -1;
    std::uint32_t name_symbol = 0;
    std::uint32_t class_symbol = 0;
    std::string name;
    std::string entity_class;
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
    // Textures of each material in the order of its `texture` record, as
    // indices of texture_names; indexed like LevelTriangle::material. Empty
    // for a material without a texture.
    std::vector<std::vector<std::int32_t>> material_textures;
    std::vector<CellAttributeTable> level_attributes;
    std::vector<ModelRange> model_ranges;
    std::vector<LevelCollision> collisions;
    // World-space collision boxes from entity models (append_entity_geometry).
    std::vector<LevelCollisionBox> collision_boxes;
    std::vector<ModelSkeleton> skeletons;
    ActorRange actor;
    std::vector<LevelInstance> instances;
    // Cell index of each instance, from the cells' `terrains` lists; -1 unknown.
    std::vector<std::int32_t> instance_cells;
    // First placed vertex of each instance (place_instances); UINT32_MAX for
    // an instance that was skipped.
    std::vector<std::uint32_t> instance_vertex_base;
    std::vector<LevelLight> lights;
    std::vector<LevelMarker> markers;
    // The clips of the `cellanim` section, in file order. A UI scene such as
    // `ui/hud` keeps the clip of each animated model here (the fill of a bar).
    std::vector<AnimReel> cell_animations;
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
CollisionModel parse_collmodc(const std::vector<std::uint8_t>& data);
// The broad-phase boxes of a `collmodc` payload: its face AABBs plus the
// whole-model AABB, deduplicated. These are not the collision hull; use
// parse_collmodc for the triangles.
std::vector<LevelCollisionBox> parse_collmodc_boxes(const std::vector<std::uint8_t>& data);

// Parses a `modlskel`/`skel` chunk: `u32 count` followed by `count` 104-byte
// bone records (FUN_0067bdd0). Throws ParseError on a truncated or oversized
// array.
std::vector<SkeletonBone> parse_skeleton(const std::vector<std::uint8_t>& data);
Vec3 rotate_euler(const Vec3& value, const Vec3& rotation);
void assign_model_lightmaps(LevelGeometry& geometry,
                            const std::vector<LevelLightMap>& lightmaps);
void assign_named_model_lightmaps(LevelGeometry& geometry,
                                  const std::vector<LevelLightMap>& lightmaps,
                                  const std::string& model_name);

}  // namespace bo2::engine
