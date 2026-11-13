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

// The nearest triangle to a sphere centre. `normal` points from the surface to
// the centre, so it is the push-out direction; the run probe uses it to slide
// along the blocker (FUN_0041ee60 projects the move against it).
struct SphereContact {
    Vec3 point{0.0f, 0.0f, 0.0f};
    Vec3 normal{0.0f, 0.0f, 1.0f};
    float distance = 0.0f;
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
    // Nearest contact of a sphere with the hull, or nothing. The `cell`
    // filter is the same as `intersects_sphere`.
    std::optional<SphereContact> sphere_contact(const Vec3& center, float radius,
                                                std::int32_t cell) const;
    // Nearest contact of a vertical capsule: the segment runs from `base` up
    // `height`, with `radius` around it. Used for the player body.
    std::optional<SphereContact> capsule_contact(const Vec3& base, float height, float radius,
                                                 std::int32_t cell) const;
    // First contact of a sphere swept along `offset` (FUN_005be9a0: the engine
    // shape cast). `RayHit::distance` is the distance travelled along `offset`;
    // the normal points from the surface to the sphere.
    std::optional<RayHit> swept_sphere(const Vec3& origin, const Vec3& offset, float radius,
                                       std::int32_t cell) const;
    // Deepest overlap of an axis-aligned box with the hull. For a box `point`
    // is the closest point on the triangle, `normal` the minimum-translation
    // axis oriented toward the box centre and `distance` the penetration
    // depth (0 when the faces only touch). The bot's body is the model AABB
    // (-20,-20,0)..(20,20,72) of Kain's `collmodc`.
    std::optional<SphereContact> box_contact(const Vec3& center, const Vec3& half_extents,
                                             std::int32_t cell) const;
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

    bool cell_allows(std::int32_t cell, std::int32_t triangle_cell) const;
};

}  // namespace bo2::engine
