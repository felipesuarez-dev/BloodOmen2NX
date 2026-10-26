#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "bo2/engine/math.hpp"

namespace bo2::engine {

struct Plane {
    Vec3 normal{0.0f, 0.0f, 1.0f};
    float distance = 0.0f;
};

struct Frustum {
    std::array<Plane, 6> planes{};

    bool intersects_sphere(const Vec3& center, float radius) const;
};

Frustum frustum_from_view_projection(const Mat4& matrix);

using EntityId = std::uint64_t;

struct Entity {
    EntityId id = 0;
    Vec3 position{0.0f, 0.0f, 0.0f};
    float radius = 0.5f;
    std::uint32_t cell = 0;
};

class EntityWorld {
public:
    EntityId add(const Vec3& position, float radius, std::uint32_t cell);
    bool remove(EntityId id);
    bool set_position(EntityId id, const Vec3& position);
    bool set_cell(EntityId id, std::uint32_t cell);
    const std::vector<Entity>& entities() const;
    std::vector<EntityId> visible(const Frustum& frustum) const;
    std::vector<EntityId> in_cell(std::uint32_t cell) const;

private:
    std::vector<Entity> entities_;
    EntityId next_id_ = 1;
};

struct CellBounds {
    std::uint32_t cell = 0;
    Vec3 minimum{0.0f, 0.0f, 0.0f};
    Vec3 maximum{0.0f, 0.0f, 0.0f};
};

class CellStreamer {
public:
    explicit CellStreamer(std::vector<CellBounds> cells, float hysteresis = 0.1f);

    const std::vector<std::uint32_t>& update(const Vec3& camera, float distance);
    const std::vector<std::uint32_t>& active() const;
    bool is_active(std::uint32_t cell) const;
    const std::vector<CellBounds>& cells() const;

private:
    std::vector<CellBounds> cells_;
    std::vector<std::uint32_t> active_;
    float hysteresis_;
};

}  // namespace bo2::engine
