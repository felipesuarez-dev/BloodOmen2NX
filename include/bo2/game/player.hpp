#pragma once

#include "bo2/engine/collision.hpp"
#include "bo2/engine/world.hpp"

#include <vector>
#include "bo2/engine/math.hpp"
#include "bo2/game/kain_tuning.hpp"

namespace bo2::game {

struct PlayerInput {
    float move_x = 0.0f;  // tank turn; same sign as the original ax: D is +1 (lowers yaw), A is -1
    float move_y = 0.0f;  // positive forward (W), negative backward (S)
    float look_x = 0.0f;
    float look_y = 0.0f;
    bool run = false;   // Shift: 200 u/s forward (measured in the Wine oracle, unverified)
    bool jump = false;  // press launches; keep it down for the boost toward height_hold
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
    // Throws std::invalid_argument when the tuning cannot drive a jump.
    PlayerController(bo2::engine::CollisionWorld& collision, const bo2::engine::Vec3& start,
                     const KainTuning& tuning);

    void step(const PlayerInput& input, double seconds);
    void look(float yaw_delta, float pitch_delta);
    bool attack(std::vector<std::uint64_t>& targets, const bo2::engine::EntityWorld& world) const;
    void restore(const PlayerState& state);
    const PlayerState& state() const;
    bo2::engine::Vec3 camera_eye() const;
    bo2::engine::Vec3 camera_target() const;

private:
    bo2::engine::CollisionWorld& collision_;
    KainTuning tuning_;
    JumpDerived jump_;
    PlayerState state_;
    float forward_time_ = 0.0f;  // seconds forward has been held (ground speed ramp)
    float air_time_ = 0.0f;      // seconds since takeoff
    bool boost_armed_ = false;   // jump held since takeoff
    bool jump_was_down_ = false;  // jump input of the previous step (press edge)
};

}  // namespace bo2::game
