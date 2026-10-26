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

class CollisionWorld {
public:
    explicit CollisionWorld(const LevelGeometry& geometry);

    std::size_t triangles() const;
    bool intersects_sphere(const Vec3& center, float radius) const;
    std::optional<RayHit> raycast(const Vec3& origin, const Vec3& direction,
                                  float max_distance) const;

private:
    struct Triangle {
        Vec3 a;
        Vec3 b;
        Vec3 c;
        Vec3 minimum;
        Vec3 maximum;
    };

    std::vector<Triangle> triangles_;
};

}  // namespace bo2::engine
