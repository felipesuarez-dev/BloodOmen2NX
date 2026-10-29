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
    // Throws std::invalid_argument when the tuning cannot drive a jump or has a
    // non-positive run rate or root speed.
    PlayerController(bo2::engine::CollisionWorld& collision, const bo2::engine::Vec3& start,
                     const KainTuning& tuning);

    void step(const PlayerInput& input, double seconds);
    void look(float yaw_delta, float pitch_delta);
    bool attack(std::vector<std::uint64_t>& targets, const bo2::engine::EntityWorld& world) const;
    void restore(const PlayerState& state);
    const PlayerState& state() const;
    // Horizontal velocity of a jump in world axes, zero on the ground and in a fall.
    bo2::engine::Vec2 air_velocity() const;
    bo2::engine::Vec3 camera_eye() const;
    bo2::engine::Vec3 camera_target() const;

private:
    bo2::engine::CollisionWorld& collision_;
    KainTuning tuning_;
    JumpDerived jump_;
    JumpDerived side_jump_;  // the vertical model of a sideways jump
    AirSteerDerived steer_forward_;
    AirSteerDerived steer_side_;
    AirSteerDerived steer_back_;
    float run_speed_ = 0.0f;   // run root speed * run clip rate (FUN_00445ba0)
    float back_speed_ = 0.0f;  // walk-back root speed * clip node speed
    PlayerState state_;
    float forward_time_ = 0.0f;  // seconds forward has been held (ground speed ramp)
    float air_time_ = 0.0f;      // seconds since takeoff
    bool boost_armed_ = false;   // jump held since takeoff
    bool jump_was_down_ = false;  // jump input of the previous step (press edge)
    // Air control of a jump (FUN_004436e0 at takeoff, FUN_00441f20 every tick).
    bool air_jump_ = false;       // airborne from a jump; a plain fall keeps the ground speed
    int air_kind_ = 0;            // 0 none, 1 forward, 2 back, 3 left, 4 right (KainState+0x540 classes)
    float air_max_forward_ = 0.0f;  // behavior+0x10c
    float air_max_side_ = 0.0f;     // behavior+0x110
    float air_max_back_ = 0.0f;     // behavior+0x114
    bo2::engine::Vec2 air_velocity_;
    bool side_jump_active_ = false;  // the jump launched with the sideVertJump model (kinds 3 and 4)

    void start_jump(const PlayerInput& input);
    void end_jump();
    void install_air_kind(int kind);
    void steer_in_air(const PlayerInput& input, float dt);
};

}  // namespace bo2::game
