#pragma once

#include <cstddef>
#include <optional>

#include "bo2/engine/cell_group.hpp"
#include "bo2/engine/math.hpp"

namespace bo2::engine {

struct RayHit {
    float distance = 0.0f;
    Vec3 point{0.0f, 0.0f, 0.0f};
    Vec3 normal{0.0f, 0.0f, 1.0f};
};

// Which mesh the collision world queries. `Collmodc` is the original's static
// collision hull; `Render` is the visible `rmd` mesh and `Boxes` is the
// `collmodc` broad-phase AABB list. The last two are diagnostics; gameplay
// collision uses `Collmodc`.
enum class CollisionSource { Collmodc, Render, Boxes };

class CollisionWorld {
public:
    explicit CollisionWorld(const LevelGeometry& geometry,
                            CollisionSource source = CollisionSource::Render);

    std::size_t triangles() const;
    std::size_t boxes() const;
    bool intersects_sphere(const Vec3& center, float radius) const;
    // Cell broad phase: the query only tests the objects of `cell` and its
    // portal neighbours (FUN_005be8b0). Pass the mover's cell, not the
    // sample's, for the probes.
    bool intersects_sphere(const Vec3& center, float radius, std::int32_t cell) const;
    std::int32_t cell_at(const Vec3& point) const;
    std::optional<RayHit> raycast(const Vec3& origin, const Vec3& direction,
                                  float max_distance) const;

private:
    struct Triangle {
        Vec3 a;
        Vec3 b;
        Vec3 c;
        Vec3 minimum;
        Vec3 maximum;
        std::int32_t cell = -1;
    };

    std::vector<Triangle> triangles_;
    std::vector<LevelCell> cells_;
    std::vector<std::vector<std::int32_t>> cell_neighbours_;
    bool cell_filter_ = false;
    std::vector<LevelCollisionBox> boxes_;
};

}  // namespace bo2::engine
