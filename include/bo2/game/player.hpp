#pragma once

#include "bo2/engine/collision.hpp"
#include "bo2/engine/world.hpp"

#include <vector>
#include "bo2/engine/math.hpp"

namespace bo2::game {

struct PlayerInput {
    float move_x = 0.0f;
    float move_y = 0.0f;
    float look_x = 0.0f;
    float look_y = 0.0f;
    bool run = false;
    bool jump = false;
    bool attack = false;
};

struct PlayerState {
    bo2::engine::Vec3 position{0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float vertical_velocity = 0.0f;
    bool grounded = false;
    float attack_cooldown = 0.0f;
    std::uint32_t combo = 0;
};

class PlayerController {
public:
    PlayerController(bo2::engine::CollisionWorld& collision, const bo2::engine::Vec3& start);

    void step(const PlayerInput& input, double seconds);
    void look(float yaw_delta, float pitch_delta);
    bool attack(std::vector<std::uint64_t>& targets, const bo2::engine::EntityWorld& world) const;
    void restore(const PlayerState& state);
    const PlayerState& state() const;
    bo2::engine::Vec3 camera_eye() const;
    bo2::engine::Vec3 camera_target() const;

private:
    bo2::engine::CollisionWorld& collision_;
    PlayerState state_;
};

}  // namespace bo2::game
