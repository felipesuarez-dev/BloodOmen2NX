#include "bo2/engine/collision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace bo2::engine {

namespace {

Vec3 closest_point_on_triangle(const Vec3& point, const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const Vec3 ap = point - a;
    const float d1 = dot(ab, ap);
    const float d2 = dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        return a;
    }
    const Vec3 bp = point - b;
    const float d3 = dot(ab, bp);
    const float d4 = dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        return b;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        return a + ab * v;
    }
    const Vec3 cp = point - c;
    const float d5 = dot(ab, cp);
    const float d6 = dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        return c;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        return a + ac * w;
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + (c - b) * w;
    }
    const float denominator = 1.0f / (va + vb + vc);
    const float v = vb * denominator;
    const float w = vc * denominator;
    return a + ab * v + ac * w;
}

float distance_squared_to_triangle(const Vec3& point, const Vec3& a, const Vec3& b,
                                   const Vec3& c) {
    const Vec3 difference = point - closest_point_on_triangle(point, a, b, c);
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

std::optional<RayHit> ray_box_hit(const Vec3& origin, const Vec3& direction,
                                  const LevelCollisionBox& box, float max_distance) {
    float t_min = 0.0f;
    float t_max = max_distance;
    int axis = -1;
    float sign = 1.0f;
    const float origin_values[3] = {origin.x, origin.y, origin.z};
    const float direction_values[3] = {direction.x, direction.y, direction.z};
    const float minimum_values[3] = {box.minimum.x, box.minimum.y, box.minimum.z};
    const float maximum_values[3] = {box.maximum.x, box.maximum.y, box.maximum.z};
    for (int index = 0; index < 3; ++index) {
        if (std::fabs(direction_values[index]) < 1.0e-8f) {
            if (origin_values[index] < minimum_values[index] ||
                origin_values[index] > maximum_values[index]) {
                return std::nullopt;
            }
            continue;
        }
        const float inverse = 1.0f / direction_values[index];
        float first = (minimum_values[index] - origin_values[index]) * inverse;
        float second = (maximum_values[index] - origin_values[index]) * inverse;
        if (first > second) {
            std::swap(first, second);
        }
        if (first > t_min) {
            t_min = first;
            axis = index;
            sign = direction_values[index] > 0.0f ? -1.0f : 1.0f;
        }
        t_max = std::min(t_max, second);
        if (t_min > t_max) {
            return std::nullopt;
        }
    }
    RayHit hit;
    hit.distance = t_min;
    hit.point = origin + direction * t_min;
    if (axis == 0) {
        hit.normal = {sign, 0.0f, 0.0f};
    } else if (axis == 1) {
        hit.normal = {0.0f, sign, 0.0f};
    } else if (axis == 2) {
        hit.normal = {0.0f, 0.0f, sign};
    } else {
        hit.normal = -direction;
    }
    return hit;
}

}  // namespace

CollisionWorld::CollisionWorld(const LevelGeometry& geometry, CollisionSource source) {
    cells_ = geometry.cell_grid.cells;
    cell_filter_ = !cells_.empty() && !geometry.instance_cells.empty();
    cell_neighbours_.assign(cells_.size(), {});
    // A portal stores symbols, not cell indices: one field is the other
    // cell's symbol and the other is the portal's own (sl01 cell01 has
    // symbol 925 and its portal is 930 <-> 931, cell02). Resolve both
    // through the cells' symbol table.
    std::uint16_t highest_symbol = 0;
    for (const LevelCell& cell : cells_) {
        highest_symbol = std::max(highest_symbol, cell.symbol);
    }
    std::vector<std::int32_t> cell_of_symbol(static_cast<std::size_t>(highest_symbol) + 1, -1);
    for (std::size_t index = 0; index < cells_.size(); ++index) {
        cell_of_symbol[cells_[index].symbol] = static_cast<std::int32_t>(index);
    }
    for (std::size_t index = 0; index < cells_.size(); ++index) {
        for (const CellPortal& portal : cells_[index].portals) {
            const auto link = [this, index](std::int32_t other) {
                if (other >= 0 && static_cast<std::size_t>(other) < cells_.size() &&
                    static_cast<std::size_t>(other) != index) {
                    cell_neighbours_[index].push_back(other);
                }
            };
            for (const std::uint16_t symbol : {portal.cell_a, portal.cell_b}) {
                if (symbol < cell_of_symbol.size()) {
                    link(cell_of_symbol[symbol]);
                }
            }
        }
    }
    const auto cell_of_instance = [&geometry](std::uint32_t instance) {
        if (instance < geometry.instance_cells.size()) {
            return geometry.instance_cells[instance];
        }
        return static_cast<std::int32_t>(-1);
    };
    const auto append_triangle = [this](const Vec3& a, const Vec3& b, const Vec3& c,
                                        std::int32_t cell) {
        Triangle value;
        value.a = a;
        value.b = b;
        value.c = c;
        value.cell = cell;
        value.minimum = {std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}),
                         std::min({a.z, b.z, c.z})};
        value.maximum = {std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}),
                         std::max({a.z, b.z, c.z})};
        triangles_.push_back(value);
    };
    if (source == CollisionSource::Render) {
        triangles_.reserve(geometry.triangles.size());
        for (const LevelTriangle& triangle : geometry.triangles) {
            if (triangle.a >= geometry.positions.size() ||
                triangle.b >= geometry.positions.size() ||
                triangle.c >= geometry.positions.size()) {
                continue;
            }
            append_triangle(geometry.positions[triangle.a], geometry.positions[triangle.b],
                            geometry.positions[triangle.c],
                            cell_of_instance(triangle.instance));
        }
    } else if (source == CollisionSource::Collmodc) {
        for (const LevelCollision& collision : geometry.collisions) {
            const CollisionModel model = parse_collmodc(collision.data);
            if (!model.valid) {
                continue;
            }
            for (const LevelInstance& instance : geometry.instances) {
                if (instance.model != collision.model) {
                    continue;
                }
                const std::int32_t cell = cell_of_instance(
                    static_cast<std::uint32_t>(&instance - geometry.instances.data()));
                const auto to_world = [&instance](const Vec3& point) {
                    const Vec3 scaled{point.x * instance.scale.x, point.y * instance.scale.y,
                                      point.z * instance.scale.z};
                    return instance.position + rotate_euler(scaled, instance.rotation);
                };
                for (const CollisionTriangle& triangle : model.triangles) {
                    if (triangle.a >= model.vertices.size() || triangle.b >= model.vertices.size() ||
                        triangle.c >= model.vertices.size()) {
                        continue;
                    }
                    append_triangle(to_world(model.vertices[triangle.a]),
                                    to_world(model.vertices[triangle.b]),
                                    to_world(model.vertices[triangle.c]), cell);
                }
            }
        }
    }
    if (source == CollisionSource::Boxes) {
        const auto append_world = [this](const LevelCollisionBox& local, const Vec3& scale,
                                         const Vec3& rotation, const Vec3& offset) {
            Vec3 minimum{std::numeric_limits<float>::infinity(),
                         std::numeric_limits<float>::infinity(),
                         std::numeric_limits<float>::infinity()};
            Vec3 maximum{-std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity()};
            for (int corner = 0; corner < 8; ++corner) {
                Vec3 point{(corner & 1) != 0 ? local.maximum.x : local.minimum.x,
                           (corner & 2) != 0 ? local.maximum.y : local.minimum.y,
                           (corner & 4) != 0 ? local.maximum.z : local.minimum.z};
                point = {point.x * scale.x, point.y * scale.y, point.z * scale.z};
                point = rotate_euler(point, rotation) + offset;
                minimum = {std::min(minimum.x, point.x), std::min(minimum.y, point.y),
                           std::min(minimum.z, point.z)};
                maximum = {std::max(maximum.x, point.x), std::max(maximum.y, point.y),
                           std::max(maximum.z, point.z)};
            }
            boxes_.push_back({minimum, maximum});
        };
        for (const LevelCollision& collision : geometry.collisions) {
            const std::vector<LevelCollisionBox> local = parse_collmodc_boxes(collision.data);
            if (local.empty()) {
                continue;
            }
            for (const LevelInstance& instance : geometry.instances) {
                if (instance.model != collision.model) {
                    continue;
                }
                for (const LevelCollisionBox& box : local) {
                    append_world(box, instance.scale, instance.rotation, instance.position);
                }
            }
        }
        boxes_.insert(boxes_.end(), geometry.collision_boxes.begin(),
                      geometry.collision_boxes.end());
    }
}

std::size_t CollisionWorld::triangles() const {
    return triangles_.size();
}

std::size_t CollisionWorld::boxes() const {
    return boxes_.size();
}

std::int32_t CollisionWorld::cell_at(const Vec3& point) const {
    for (std::size_t index = 0; index < cells_.size(); ++index) {
        const LevelCell& cell = cells_[index];
        if (point.x >= cell.minimum.x && point.x <= cell.maximum.x &&
            point.y >= cell.minimum.y && point.y <= cell.maximum.y &&
            point.z >= cell.minimum.z && point.z <= cell.maximum.z) {
            return static_cast<std::int32_t>(index);
        }
    }
    return -1;
}

bool CollisionWorld::intersects_sphere(const Vec3& center, float radius) const {
    return intersects_sphere(center, radius, cell_at(center));
}

bool CollisionWorld::cell_allows(std::int32_t cell, std::int32_t triangle_cell) const {
    if (!cell_filter_ || triangle_cell < 0 || cell < 0) {
        return true;
    }
    if (triangle_cell == cell) {
        return true;
    }
    for (const std::int32_t neighbour : cell_neighbours_[static_cast<std::size_t>(cell)]) {
        if (neighbour == triangle_cell) {
            return true;
        }
    }
    return false;
}

bool CollisionWorld::intersects_sphere(const Vec3& center, float radius,
                                       std::int32_t cell) const {
    if (!std::isfinite(radius) || radius < 0.0f) {
        throw std::invalid_argument("sphere radius must be non-negative");
    }
    const float radius_squared = radius * radius;
    for (const Triangle& triangle : triangles_) {
        if (!cell_allows(cell, triangle.cell)) {
            continue;
        }
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
    for (const LevelCollisionBox& box : boxes_) {
        const Vec3 closest{std::clamp(center.x, box.minimum.x, box.maximum.x),
                           std::clamp(center.y, box.minimum.y, box.maximum.y),
                           std::clamp(center.z, box.minimum.z, box.maximum.z)};
        const Vec3 difference = center - closest;
        if (dot(difference, difference) <= radius_squared) {
            return true;
        }
    }
    return false;
}

std::optional<SphereContact> CollisionWorld::sphere_contact(const Vec3& center, float radius,
                                                            std::int32_t cell) const {
    if (!std::isfinite(radius) || radius < 0.0f) {
        throw std::invalid_argument("sphere radius must be non-negative");
    }
    std::optional<SphereContact> best;
    float best_squared = radius * radius;
    for (const Triangle& triangle : triangles_) {
        if (!cell_allows(cell, triangle.cell)) {
            continue;
        }
        const Vec3 box_point{std::clamp(center.x, triangle.minimum.x, triangle.maximum.x),
                             std::clamp(center.y, triangle.minimum.y, triangle.maximum.y),
                             std::clamp(center.z, triangle.minimum.z, triangle.maximum.z)};
        const Vec3 box_difference = center - box_point;
        if (dot(box_difference, box_difference) > best_squared) {
            continue;
        }
        const Vec3 point = closest_point_on_triangle(center, triangle.a, triangle.b, triangle.c);
        const Vec3 difference = center - point;
        const float squared = dot(difference, difference);
        if (squared > best_squared) {
            continue;
        }
        best_squared = squared;
        Vec3 normal;
        if (squared > 1.0e-6f) {
            normal = difference * (1.0f / std::sqrt(squared));
        } else {
            // The centre lies on the face and `difference` is rounding noise:
            // use the face normal, oriented toward the centre.
            normal = normalize(cross(triangle.b - triangle.a, triangle.c - triangle.a));
            if (dot(normal, difference) < 0.0f) {
                normal = normal * -1.0f;
            }
        }
        best = SphereContact{point, normal, std::sqrt(squared)};
    }
    return best;
}

std::optional<SphereContact> CollisionWorld::capsule_contact(const Vec3& base, float height,
                                                            float radius,
                                                            std::int32_t cell) const {
    if (!std::isfinite(radius) || radius < 0.0f || !std::isfinite(height) || height < 0.0f) {
        throw std::invalid_argument("capsule radius and height must be non-negative");
    }
    std::optional<SphereContact> best;
    float best_squared = radius * radius;
    const float top = base.z + height;
    for (const Triangle& triangle : triangles_) {
        if (!cell_allows(cell, triangle.cell)) {
            continue;
        }
        if (triangle.maximum.z < base.z - radius || triangle.minimum.z > top + radius) {
            continue;
        }
        // Horizontal distance from the segment axis to the triangle AABB.
        const float dx = std::max({triangle.minimum.x - base.x, 0.0f, base.x - triangle.maximum.x});
        const float dy = std::max({triangle.minimum.y - base.y, 0.0f, base.y - triangle.maximum.y});
        if (dx * dx + dy * dy > radius * radius) {
            continue;
        }
        const float low = std::max(base.z, triangle.minimum.z - radius);
        const float high = std::min(top, triangle.maximum.z + radius);
        if (low > high) {
            continue;
        }
        // For a vertical segment the closest point lies at the segment ends or
        // where the triangle's own height range starts and ends.
        const float heights[5] = {base.z, top, low, high, 0.5f * (low + high)};
        for (const float z : heights) {
            const Vec3 center{base.x, base.y, z};
            const Vec3 point =
                closest_point_on_triangle(center, triangle.a, triangle.b, triangle.c);
            const Vec3 difference = center - point;
            const float squared = dot(difference, difference);
            if (squared > best_squared) {
                continue;
            }
            best_squared = squared;
            Vec3 normal;
            if (squared > 1.0e-6f) {
                normal = difference * (1.0f / std::sqrt(squared));
            } else {
                normal = normalize(cross(triangle.b - triangle.a, triangle.c - triangle.a));
                if (dot(normal, difference) < 0.0f) {
                    normal = normal * -1.0f;
                }
            }
            best = SphereContact{point, normal, std::sqrt(squared)};
        }
    }
    return best;
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
    for (const LevelCollisionBox& box : boxes_) {
        const std::optional<RayHit> box_hit = ray_box_hit(origin, ray, box, best);
        if (!box_hit.has_value() || box_hit->distance >= best) {
            continue;
        }
        best = box_hit->distance;
        hit = box_hit;
    }
    return hit;
}

}  // namespace bo2::engine
