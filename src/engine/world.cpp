#include "bo2/engine/world.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace bo2::engine {

bool Frustum::intersects_sphere(const Vec3& center, float radius) const {
    for (const Plane& plane : planes) {
        if (dot(plane.normal, center) + plane.distance < -radius) {
            return false;
        }
    }
    return true;
}

Frustum frustum_from_view_projection(const Mat4& matrix) {
    const Vec3 row0{matrix.m[0], matrix.m[1], matrix.m[2]};
    const Vec3 row1{matrix.m[4], matrix.m[5], matrix.m[6]};
    const Vec3 row2{matrix.m[8], matrix.m[9], matrix.m[10]};
    const Vec3 row3{matrix.m[12], matrix.m[13], matrix.m[14]};
    const float w0 = matrix.m[3];
    const float w1 = matrix.m[7];
    const float w2 = matrix.m[11];
    const float w3 = matrix.m[15];
    const auto make_plane = [](const Vec3& normal, float distance) {
        Plane plane;
        plane.normal = normal;
        plane.distance = distance;
        const float magnitude = length(normal);
        if (magnitude > 0.0f) {
            plane.normal = plane.normal / magnitude;
            plane.distance /= magnitude;
        }
        return plane;
    };
    Frustum frustum;
    frustum.planes[0] = make_plane(row3 + row0, w3 + w0);
    frustum.planes[1] = make_plane(row3 - row0, w3 - w0);
    frustum.planes[2] = make_plane(row3 + row1, w3 + w1);
    frustum.planes[3] = make_plane(row3 - row1, w3 - w1);
    frustum.planes[4] = make_plane(row3 + row2, w3 + w2);
    frustum.planes[5] = make_plane(row3 - row2, w3 - w2);
    return frustum;
}

EntityId EntityWorld::add(const Vec3& position, float radius, std::uint32_t cell) {
    if (!std::isfinite(radius) || radius < 0.0f) {
        throw std::invalid_argument("entity radius must be non-negative");
    }
    Entity entity;
    entity.id = next_id_++;
    entity.position = position;
    entity.radius = radius;
    entity.cell = cell;
    entities_.push_back(entity);
    return entity.id;
}

bool EntityWorld::remove(EntityId id) {
    const auto found = std::find_if(entities_.begin(), entities_.end(),
                                    [id](const Entity& entity) { return entity.id == id; });
    if (found == entities_.end()) {
        return false;
    }
    entities_.erase(found);
    return true;
}

bool EntityWorld::set_position(EntityId id, const Vec3& position) {
    for (Entity& entity : entities_) {
        if (entity.id == id) {
            entity.position = position;
            return true;
        }
    }
    return false;
}

bool EntityWorld::set_cell(EntityId id, std::uint32_t cell) {
    for (Entity& entity : entities_) {
        if (entity.id == id) {
            entity.cell = cell;
            return true;
        }
    }
    return false;
}

const std::vector<Entity>& EntityWorld::entities() const {
    return entities_;
}

std::vector<EntityId> EntityWorld::visible(const Frustum& frustum) const {
    std::vector<EntityId> result;
    for (const Entity& entity : entities_) {
        if (frustum.intersects_sphere(entity.position, entity.radius)) {
            result.push_back(entity.id);
        }
    }
    return result;
}

std::vector<EntityId> EntityWorld::in_cell(std::uint32_t cell) const {
    std::vector<EntityId> result;
    for (const Entity& entity : entities_) {
        if (entity.cell == cell) {
            result.push_back(entity.id);
        }
    }
    return result;
}

namespace {

float distance_to_bounds(const Vec3& point, const CellBounds& bounds) {
    const Vec3 closest{std::clamp(point.x, bounds.minimum.x, bounds.maximum.x),
                       std::clamp(point.y, bounds.minimum.y, bounds.maximum.y),
                       std::clamp(point.z, bounds.minimum.z, bounds.maximum.z)};
    return length(point - closest);
}

}  // namespace

CellStreamer::CellStreamer(std::vector<CellBounds> cells, float hysteresis)
    : cells_(std::move(cells)), hysteresis_(std::max(0.0f, hysteresis)) {
    for (const CellBounds& bounds : cells_) {
        if (bounds.minimum.x > bounds.maximum.x || bounds.minimum.y > bounds.maximum.y ||
            bounds.minimum.z > bounds.maximum.z) {
            throw std::invalid_argument("cell bounds are inverted");
        }
    }
}

const std::vector<std::uint32_t>& CellStreamer::update(const Vec3& camera, float distance) {
    if (!std::isfinite(distance) || distance < 0.0f) {
        throw std::invalid_argument("streaming distance must be non-negative");
    }
    std::vector<std::uint32_t> next;
    for (const CellBounds& bounds : cells_) {
        const bool was_active =
            std::find(active_.begin(), active_.end(), bounds.cell) != active_.end();
        const float limit = was_active ? distance * (1.0f + hysteresis_) : distance;
        if (distance_to_bounds(camera, bounds) <= limit) {
            next.push_back(bounds.cell);
        }
    }
    active_ = std::move(next);
    return active_;
}

const std::vector<std::uint32_t>& CellStreamer::active() const {
    return active_;
}

bool CellStreamer::is_active(std::uint32_t cell) const {
    return std::find(active_.begin(), active_.end(), cell) != active_.end();
}

const std::vector<CellBounds>& CellStreamer::cells() const {
    return cells_;
}

}  // namespace bo2::engine
