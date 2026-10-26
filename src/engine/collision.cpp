#include "bo2/engine/collision.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bo2::engine {

namespace {

float distance_squared_to_triangle(const Vec3& point, const Vec3& a, const Vec3& b,
                                   const Vec3& c) {
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const Vec3 ap = point - a;
    const float d1 = dot(ab, ap);
    const float d2 = dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        return dot(ap, ap);
    }
    const Vec3 bp = point - b;
    const float d3 = dot(ab, bp);
    const float d4 = dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        return dot(bp, bp);
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        const Vec3 closest = a + ab * v;
        const Vec3 difference = point - closest;
        return dot(difference, difference);
    }
    const Vec3 cp = point - c;
    const float d5 = dot(ab, cp);
    const float d6 = dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        return dot(cp, cp);
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        const Vec3 closest = a + ac * w;
        const Vec3 difference = point - closest;
        return dot(difference, difference);
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        const Vec3 closest = b + (c - b) * w;
        const Vec3 difference = point - closest;
        return dot(difference, difference);
    }
    const float denominator = 1.0f / (va + vb + vc);
    const float v = vb * denominator;
    const float w = vc * denominator;
    const Vec3 closest = a + ab * v + ac * w;
    const Vec3 difference = point - closest;
    return dot(difference, difference);
}

bool ray_hits_aabb(const Vec3& origin, const Vec3& direction, const Vec3& minimum,
                   const Vec3& maximum, float max_distance) {
    float t_min = 0.0f;
    float t_max = max_distance;
    const float origin_values[3] = {origin.x, origin.y, origin.z};
    const float direction_values[3] = {direction.x, direction.y, direction.z};
    const float minimum_values[3] = {minimum.x, minimum.y, minimum.z};
    const float maximum_values[3] = {maximum.x, maximum.y, maximum.z};
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(direction_values[axis]) < 1.0e-8f) {
            if (origin_values[axis] < minimum_values[axis] ||
                origin_values[axis] > maximum_values[axis]) {
                return false;
            }
            continue;
        }
        const float inverse = 1.0f / direction_values[axis];
        float first = (minimum_values[axis] - origin_values[axis]) * inverse;
        float second = (maximum_values[axis] - origin_values[axis]) * inverse;
        if (first > second) {
            std::swap(first, second);
        }
        t_min = std::max(t_min, first);
        t_max = std::min(t_max, second);
        if (t_min > t_max) {
            return false;
        }
    }
    return true;
}

}  // namespace

CollisionWorld::CollisionWorld(const LevelGeometry& geometry) {
    triangles_.reserve(geometry.triangles.size());
    for (const LevelTriangle& triangle : geometry.triangles) {
        if (triangle.a >= geometry.positions.size() || triangle.b >= geometry.positions.size() ||
            triangle.c >= geometry.positions.size()) {
            continue;
        }
        const Vec3 a = geometry.positions[triangle.a];
        const Vec3 b = geometry.positions[triangle.b];
        const Vec3 c = geometry.positions[triangle.c];
        Triangle value;
        value.a = a;
        value.b = b;
        value.c = c;
        value.minimum = {std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}),
                         std::min({a.z, b.z, c.z})};
        value.maximum = {std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}),
                         std::max({a.z, b.z, c.z})};
        triangles_.push_back(value);
    }
}

std::size_t CollisionWorld::triangles() const {
    return triangles_.size();
}

bool CollisionWorld::intersects_sphere(const Vec3& center, float radius) const {
    if (!std::isfinite(radius) || radius < 0.0f) {
        throw std::invalid_argument("sphere radius must be non-negative");
    }
    const float radius_squared = radius * radius;
    for (const Triangle& triangle : triangles_) {
        const Vec3 closest{std::clamp(center.x, triangle.minimum.x, triangle.maximum.x),
                           std::clamp(center.y, triangle.minimum.y, triangle.maximum.y),
                           std::clamp(center.z, triangle.minimum.z, triangle.maximum.z)};
        const Vec3 difference = center - closest;
        if (dot(difference, difference) > radius_squared) {
            continue;
        }
        if (distance_squared_to_triangle(center, triangle.a, triangle.b, triangle.c) <=
            radius_squared) {
            return true;
        }
    }
    return false;
}

std::optional<RayHit> CollisionWorld::raycast(const Vec3& origin, const Vec3& direction,
                                              float max_distance) const {
    if (!std::isfinite(max_distance) || max_distance < 0.0f) {
        throw std::invalid_argument("ray distance must be non-negative");
    }
    const Vec3 ray = normalize(direction);
    if (length(ray) == 0.0f) {
        return std::nullopt;
    }
    float best = max_distance;
    std::optional<RayHit> hit;
    for (const Triangle& triangle : triangles_) {
        if (!ray_hits_aabb(origin, ray, triangle.minimum, triangle.maximum, best)) {
            continue;
        }
        const Vec3 edge1 = triangle.b - triangle.a;
        const Vec3 edge2 = triangle.c - triangle.a;
        const Vec3 p = cross(ray, edge2);
        const float determinant = dot(edge1, p);
        if (std::fabs(determinant) < 1.0e-8f) {
            continue;
        }
        const float inverse = 1.0f / determinant;
        const Vec3 t = origin - triangle.a;
        const float u = dot(t, p) * inverse;
        if (u < 0.0f || u > 1.0f) {
            continue;
        }
        const Vec3 q = cross(t, edge1);
        const float v = dot(ray, q) * inverse;
        if (v < 0.0f || u + v > 1.0f) {
            continue;
        }
        const float distance = dot(edge2, q) * inverse;
        if (distance < 0.0f || distance >= best) {
            continue;
        }
        best = distance;
        Vec3 normal = normalize(cross(edge1, edge2));
        if (dot(normal, ray) > 0.0f) {
            normal = -normal;
        }
        hit = RayHit{best, origin + ray * best, normal};
    }
    return hit;
}

}  // namespace bo2::engine
