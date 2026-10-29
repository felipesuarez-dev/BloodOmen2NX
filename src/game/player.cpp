#include "bo2/game/player.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bo2::game {

namespace {

// Ground speed is animation root motion in the original (FUN_0041f7d0 adds
// the root delta of FUN_0041fb00 to the position). Two speeds are derived from
// the clips (docs/FORMATS/animreel.md, docs/GAMEPLAY.md):
//   W at full stick: kain/run root speed (200 u/s) * the run clip rate
//     A+0x40 (1.15), FUN_00445ba0 slot 0. Confirmed by the Wine oracle.
//   S: kain/walk_backwards root speed (62.5 u/s) * the speed of its clip node
//     in the `kain` animatio graph (1.7, docs/FORMATS/animatio.md). Confirmed
//     by the Wine oracle (106.25).
// Partial stick (FUN_00445ba0 blends cl_run on layer 0 with cl_walk on
// layer 1) is not ported.
// The rest are measured in the Wine oracle
// (external/captures/wine_20261029_0052/state.csv, world-frame |vel_xy|) and
// not explained by a decompiled function yet.
constexpr float kRunStartSpeed = 100.0f;   // measured, W for the first kRunStartTime (= 200 * 0.5, unexplained)
constexpr float kRunStartTime = 0.18f;     // measured
// Measured but unverified: Kain was probably blocked for part of that capture
// (docs/ORACLE_WINE.md), so this is not a confirmed top speed. 200 = 200 * 1.0
// is unexplained.
constexpr float kShiftSpeed = 200.0f;      // measured, Shift+W

constexpr float kMaxTurnScale = 4.0f;  // FUN_0041ecf0 caps |ax| / (1 - deadzone) at 4

// DAT_00844c00: degrees to radians. FUN_00441f20 turns a forward or back jump
// by `jump_turn_rate` degrees per second and skips the speed update on a tick
// that turns by one degree or more.
constexpr float kDegrees = 0.017453292519943295f;

// FUN_00425580: the stick class that picks the jump direction (it is stored at
// KainState+0x540 every tick). `ax` is positive for D, `ay` is negative for W.
// 0 neutral, 1 forward, 2 back, 3 left, 4 right.
int stick_class(float ax, float ay, float deadzone) {
    if (std::sqrt(ax * ax + ay * ay) < deadzone) {
        return 0;
    }
    const float wedge = 2.0f * std::fabs(ay);  // the forward and back cones are 2 |ay| wide
    if (ax > wedge) {
        return 4;
    }
    if (-wedge <= ax) {
        return ay >= 0.0f ? 2 : 1;
    }
    return 3;
}

// FUN_00442240: the speed the stick asks for on one axis, `limit_positive` for
// a stick value that is negative and `limit_negative` for a positive one. The
// stick is rescaled from the deadzone to 1.
float stick_target(float stick, float deadzone, float limit_negative_stick, float limit_positive_stick) {
    if (!(stick > deadzone || stick < -deadzone)) {
        return 0.0f;
    }
    const float range = 1.0f - deadzone;
    if (stick >= 0.0f) {
        return -(((stick - deadzone) / range) * limit_positive_stick);
    }
    return (limit_negative_stick / range) * (-stick - deadzone);
}

// FUN_004423b0: moves the (forward, left) velocity toward the targets. With a
// zero target the velocity is first multiplied by the damping of its sign,
// once per call, so that part depends on the tick rate as in the original.
void approach_targets(bo2::engine::Vec2& velocity, float dt, const AirSteerDerived& forward,
                      const AirSteerDerived& back, const AirSteerDerived& side, float target_forward,
                      float target_side) {
    if (target_forward == 0.0f) {
        velocity.x *= velocity.x >= 0.0f ? forward.damping : back.damping;
    }
    if (target_forward < velocity.x) {
        velocity.x = std::max(target_forward, velocity.x - dt * back.accel);
    }
    if (velocity.x < target_forward) {
        velocity.x = std::min(target_forward, dt * forward.accel + velocity.x);
    }
    if (target_side == 0.0f) {
        velocity.y *= side.damping;
    }
    if (target_side < velocity.y) {
        velocity.y = std::max(target_side, velocity.y - dt * side.accel);
    }
    if (velocity.y < target_side) {
        velocity.y = std::min(target_side, dt * side.accel + velocity.y);
    }
}

// FUN_00694970: rotates a vector counter-clockwise.
bo2::engine::Vec2 rotate(const bo2::engine::Vec2& value, float angle) {
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    return {cosine * value.x - sine * value.y, sine * value.x + cosine * value.y};
}

// A step longer than this is cut short, the same cap the SDL loop applies, so
// a stalled frame cannot throw Kain through a wall or the floor.
constexpr double kMaxStepSeconds = 0.1;
constexpr int kMaxSubSteps = 64;  // 0.1 s at 230 u/s needs 58

// Provisional: these predate the Kain tuning, are in meter-like units (Kain's
// units are about 100x larger) and still have to be recovered.
constexpr float kEyeHeight = 1.7f;
constexpr float kBodyRadius = 0.4f;
constexpr float kBodyClearance = 0.05f;
constexpr float kLookSpeed = 2.0f;  // port debug aid (mouse and right stick look)
constexpr float kAttackCooldown = 0.45f;
constexpr float kAttackRange = 2.2f;
constexpr float kAttackRadius = 1.0f;

// Seconds of [begin, begin + dt) that fall inside [window_begin, window_end).
float window_overlap(float begin, float dt, float window_begin, float window_end) {
    const float low = std::max(begin, window_begin);
    const float high = std::min(begin + dt, window_end);
    return std::max(0.0f, high - low);
}

}  // namespace

PlayerController::PlayerController(bo2::engine::CollisionWorld& collision,
                                   const bo2::engine::Vec3& start, const KainTuning& tuning)
    : collision_(collision),
      tuning_(tuning),
      jump_(derive_jump(tuning)),
      side_jump_(derive_jump(tuning.side_jump)),
      steer_forward_(derive_air_steer(tuning.air_forward, jump_)),
      steer_side_(derive_air_steer(tuning.air_side, jump_)),
      steer_back_(derive_air_steer(tuning.air_back, jump_)) {
    if (!(tuning.stick_deadzone >= 0.0f && tuning.stick_deadzone < 1.0f) ||
        !(tuning.jump.terminal_fall > 0.0f)) {
        throw std::invalid_argument("kain tuning has an invalid deadzone or fall speed cap");
    }
    const auto positive = [](float value) { return std::isfinite(value) && value > 0.0f; };
    if (!positive(tuning.run_clip_rate) || !positive(tuning.run_root_speed) ||
        !positive(tuning.walk_back_root_speed) || !positive(tuning.walk_back_clip_speed)) {
        throw std::invalid_argument("kain tuning has an invalid run rate or root speed");
    }
    run_speed_ = tuning.run_root_speed * tuning.run_clip_rate;
    back_speed_ = tuning.walk_back_root_speed * tuning.walk_back_clip_speed;
    state_.position = start;
}

void PlayerController::step(const PlayerInput& input, double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0) {
        return;
    }
    const float dt = static_cast<float>(std::min(seconds, kMaxStepSeconds));
    const float deadzone = tuning_.stick_deadzone;
    // Forward/back gating is not traced; an axis past the deadzone counts.
    const auto active = [deadzone](float axis) {
        return axis != 0.0f && std::fabs(axis) > deadzone;
    };
    const bool forward_held = active(input.move_y) && input.move_y > 0.0f;
    const bool back_held = active(input.move_y) && input.move_y < 0.0f;

    // Tank turn, FUN_0041ecf0: rate * min(|ax| / (1 - deadzone), 4). The
    // original does `if (0 < ax) yaw -= ...; else yaw += ...`, so A is a
    // negative ax and raises the yaw (measured in the oracle). The port's
    // move_x has the same sign as ax: D is +1, A is -1. Kain turns in place.
    float turn_rate = tuning_.turn_rate_ready;
    if (forward_held) {
        turn_rate = tuning_.turn_rate_run;
    } else if (back_held) {
        turn_rate = tuning_.turn_rate_walk_back;
    }
    if (air_jump_) {
        steer_in_air(input, dt);
    } else if (input.move_x != 0.0f && deadzone <= std::fabs(input.move_x)) {  // FUN_0041ecf0: `deadzone <= ax`
        const float scale =
            std::min(std::fabs(input.move_x) / (1.0f - deadzone), kMaxTurnScale);
        const float direction = input.move_x > 0.0f ? -1.0f : 1.0f;  // ax > 0 lowers the yaw
        state_.yaw += direction * scale * turn_rate * dt;
    }
    state_.yaw -= input.look_x * kLookSpeed * dt;
    state_.pitch += input.look_y * kLookSpeed * dt;
    state_.pitch = std::clamp(state_.pitch, -1.2f, 1.2f);

    // Ground speed. A jump has its own horizontal velocity (steer_in_air); a
    // plain fall keeps the ground speed of the held input. A jump leaves
    // forward_time_ as it was, so a running jump lands at the run speed while
    // a standing one goes through the start phase: the `kain` animatio graph
    // enters st_run without st_rdy2run when the previous state is a run
    // landing, and Wine capture wine_air_1 (jump_w_hold) reads 230 u/s right
    // after the landing clip. Inferred from those two; the landing clip's own
    // root motion is not modeled.
    float speed = 0.0f;
    if (air_jump_) {
        // Horizontal motion comes from steer_in_air.
    } else if (forward_held) {
        if (input.run) {
            speed = kShiftSpeed;
        } else if (forward_time_ < kRunStartTime) {
            speed = kRunStartSpeed;
        } else {
            speed = run_speed_;
        }
        forward_time_ += dt;
    } else {
        forward_time_ = 0.0f;
        if (back_held) {
            speed = -back_speed_;
        }
    }
    // Released input stops at once. The measured trace shows a short slide
    // that is not modeled.

    const bo2::engine::Vec3 heading{std::cos(state_.yaw), std::sin(state_.yaw), 0.0f};
    // The step of this tick: the jump velocity, or the ground speed along the yaw.
    const bo2::engine::Vec3 displacement =
        air_jump_ ? bo2::engine::Vec3{air_velocity_.x * dt, air_velocity_.y * dt, 0.0f}
                  : heading * (speed * dt);
    const float travel = bo2::engine::length(displacement);
    if (travel > 0.0f) {
        // The original body sphere is small next to a tick of travel, so walk
        // the move in sub-steps of at most one radius; otherwise a thin wall
        // can be skipped between two samples.
        const int steps = std::clamp(static_cast<int>(std::ceil(travel / kBodyRadius)), 1, kMaxSubSteps);
        const bo2::engine::Vec3 sub_step = displacement / static_cast<float>(steps);
        for (int index = 0; index < steps; ++index) {
            const bo2::engine::Vec3 next = state_.position + sub_step;
            const bo2::engine::Vec3 body_center =
                next + bo2::engine::Vec3{0.0f, 0.0f, kBodyRadius + kBodyClearance};
            if (collision_.intersects_sphere(body_center, kBodyRadius)) {
                // The original zeroes the forward or back velocity when a
                // probe is blocked (KainState+0x450/0x454); the port stops the jump.
                air_velocity_ = {0.0f, 0.0f};
                break;
            }
            state_.position = next;
        }
    }

    if (state_.attack_cooldown > 0.0f) {
        state_.attack_cooldown = std::max(0.0f, state_.attack_cooldown - dt);
    }
    if (input.attack && state_.attack_cooldown <= 0.0f) {
        state_.attack_cooldown = kAttackCooldown;
        state_.combo = (state_.combo + 1) % 3;
    }

    // Jump (FUN_004436e0, FUN_00451d00): launch at sqrt(2 * h1 * |g_up|). It
    // fires on the press edge, so the button must be released before the next
    // jump. The original trigger path (the caller of FUN_004436e0) is not
    // traced; the edge is an assumption.
    if (input.jump && !jump_was_down_ && state_.grounded) {
        start_jump(input);
    }
    jump_was_down_ = input.jump;

    // Gravity is g_up while vz >= 0 and g_down once vz < 0 (FUN_00442ae0). A
    // held jump adds boost_accel between boost_start and boost_end after
    // takeoff, averaged over the part of the tick that lies in the window.
    // A sideways jump climbs with the sideVertJump g_up and no boost, then
    // falls with the normal g_down (FUN_00442ae0 reads A+0x408).
    const JumpDerived& model = side_jump_active_ ? side_jump_ : jump_;
    const float gravity_up = side_jump_active_ ? tuning_.side_jump.gravity_up : tuning_.jump.gravity_up;
    float acceleration = state_.vertical_velocity >= 0.0f ? gravity_up : tuning_.jump.gravity_down;
    if (boost_armed_) {
        acceleration +=
            model.boost_accel * window_overlap(air_time_, dt, model.boost_start, model.boost_end) / dt;
    }
    // FUN_00443a50 accumulates the tick's boost first and only then checks the
    // button, so the tick that sees the release still gets its boost.
    if (!input.jump) {
        boost_armed_ = false;
    }
    // Integrator, FUN_00432030: pos += (v + a * dt / 2) * dt; v += a * dt.
    const float velocity = state_.vertical_velocity;
    const float climb = (velocity + 0.5f * acceleration * dt) * dt;
    float next_velocity = velocity + acceleration * dt;
    // Fall speed cap, FUN_00441f20.
    next_velocity = std::max(next_velocity, -tuning_.jump.terminal_fall);

    // The probe covers the distance fallen this tick so a fast fall cannot
    // skip the floor.
    const float fall = std::max(0.0f, -climb);
    const bo2::engine::Vec3 ray_origin =
        state_.position + bo2::engine::Vec3{0.0f, 0.0f, kEyeHeight};
    const auto ground =
        collision_.raycast(ray_origin, {0.0f, 0.0f, -1.0f}, kEyeHeight + 1.0f + fall);
    state_.position.z += climb;
    state_.vertical_velocity = next_velocity;
    if (ground.has_value() && state_.position.z <= ground->point.z) {
        state_.position.z = ground->point.z;
        state_.vertical_velocity = 0.0f;
        state_.grounded = true;
        boost_armed_ = false;
        air_time_ = 0.0f;
        end_jump();
    } else {
        state_.grounded = false;
        air_time_ += dt;
    }
}

// FUN_004436e0: the takeoff. The stick class (FUN_00425580) picks the
// direction; the speed is the stick magnitude times the launch speed of that
// direction's block, and a sideways jump swaps in the sideVertJump model. The
// original then rotates the local velocity by the yaw (FUN_00694970).
void PlayerController::start_jump(const PlayerInput& input) {
    const float stick_x = input.move_x;
    const float stick_y = -input.move_y;  // the original's stick y is negative for W
    const int kind = stick_class(stick_x, stick_y, tuning_.stick_deadzone);
    bo2::engine::Vec2 local;
    side_jump_active_ = false;
    switch (kind) {
        case 1:
            local.x = std::fabs(stick_y) * steer_forward_.launch_speed;
            break;
        case 2:
            local.x = -(std::fabs(stick_y) * steer_back_.launch_speed);
            break;
        case 3:
            local.y = std::fabs(stick_x) * steer_side_.launch_speed;
            side_jump_active_ = true;
            break;
        case 4:
            local.y = -(std::fabs(stick_x) * steer_side_.launch_speed);
            side_jump_active_ = true;
            break;
        default:
            break;
    }
    air_kind_ = 0;
    if (kind != 0) {
        install_air_kind(kind);
    }
    air_velocity_ = rotate(local, state_.yaw);
    air_jump_ = true;

    state_.vertical_velocity = (side_jump_active_ ? side_jump_ : jump_).launch_speed;
    state_.grounded = false;
    air_time_ = 0.0f;
    boost_armed_ = true;
}

void PlayerController::end_jump() {
    air_jump_ = false;
    air_kind_ = 0;
    air_velocity_ = {0.0f, 0.0f};
    side_jump_active_ = false;
}

// FUN_00441dc0: the top speeds a jump direction allows on each axis.
void PlayerController::install_air_kind(int kind) {
    air_kind_ = kind;
    switch (kind) {
        case 1:  // forward: its own peak, nothing sideways or backward
            air_max_forward_ = steer_forward_.peak_speed;
            air_max_side_ = steer_forward_.lateral_speed;
            air_max_back_ = steer_forward_.lateral_speed;
            break;
        case 2:  // back
            air_max_forward_ = steer_back_.lateral_speed;
            air_max_side_ = steer_back_.lateral_speed;
            air_max_back_ = steer_back_.peak_speed;
            break;
        case 3:
        case 4:  // sideways, either way
            air_max_forward_ = steer_side_.lateral_speed;
            air_max_side_ = steer_side_.peak_speed;
            air_max_back_ = steer_side_.lateral_speed;
            break;
        default:
            air_max_forward_ = air_max_side_ = air_max_back_ = 0.0f;
            break;
    }
}

// FUN_00441f20, the horizontal part: the air turn of a forward or back jump
// and the velocity moving toward the speed the stick asks for.
void PlayerController::steer_in_air(const PlayerInput& input, float dt) {
    const float deadzone = tuning_.stick_deadzone;
    const float stick_x = input.move_x;
    const float stick_y = -input.move_y;
    // A jump without a direction takes the first one pressed in the air.
    if (air_kind_ == 0) {
        const int kind = stick_class(stick_x, stick_y, deadzone);
        if (kind != 0) {
            install_air_kind(kind);
        }
    }
    const bool turns = air_kind_ == 1 || air_kind_ == 2;
    float turn = 0.0f;
    if (turns && (stick_x > deadzone || stick_x < -deadzone)) {
        turn = -(kDegrees * tuning_.jump_turn_rate * stick_x * dt);
    }
    // The velocity is turned to the local frame with the old yaw, changed, and
    // turned back with the new one, so a turn carries it around with Kain.
    bo2::engine::Vec2 local = rotate(air_velocity_, -state_.yaw);
    state_.yaw += turn;
    // A tick that turns by a degree or more leaves the velocity alone.
    if (!(turns && std::fabs(turn) >= kDegrees)) {
        const float target_forward = stick_target(stick_y, deadzone, air_max_forward_, air_max_back_);
        const float target_side = stick_target(stick_x, deadzone, air_max_side_, air_max_side_);
        approach_targets(local, dt, steer_forward_, steer_back_, steer_side_, target_forward,
                         target_side);
    }
    air_velocity_ = rotate(local, state_.yaw);
}

bo2::engine::Vec2 PlayerController::air_velocity() const {
    return air_velocity_;
}

void PlayerController::look(float yaw_delta, float pitch_delta) {
    state_.yaw += yaw_delta;
    state_.pitch = std::clamp(state_.pitch + pitch_delta, -1.2f, 1.2f);
}

void PlayerController::restore(const PlayerState& state) {
    state_ = state;
    state_.vertical_velocity = 0.0f;
    forward_time_ = 0.0f;
    air_time_ = 0.0f;
    boost_armed_ = false;
    jump_was_down_ = false;
    end_jump();
}

bool PlayerController::attack(std::vector<std::uint64_t>& targets,
                              const bo2::engine::EntityWorld& world) const {
    if (state_.attack_cooldown > kAttackCooldown * 0.5f) {
        return false;
    }
    const float cosine = std::cos(state_.yaw);
    const float sine = std::sin(state_.yaw);
    const bo2::engine::Vec3 center =
        state_.position + bo2::engine::Vec3{cosine * kAttackRange, sine * kAttackRange, 1.0f};
    targets = world.in_sphere(center, kAttackRadius);
    return !targets.empty();
}

const PlayerState& PlayerController::state() const {
    return state_;
}

bo2::engine::Vec3 PlayerController::camera_eye() const {
    return state_.position + bo2::engine::Vec3{0.0f, 0.0f, kEyeHeight};
}

bo2::engine::Vec3 PlayerController::camera_target() const {
    const float cosine_pitch = std::cos(state_.pitch);
    const bo2::engine::Vec3 direction{cosine_pitch * std::cos(state_.yaw),
                                      cosine_pitch * std::sin(state_.yaw),
                                      std::sin(state_.pitch)};
    return camera_eye() + bo2::engine::normalize(direction) * 10.0f;
}

}  // namespace bo2::game
