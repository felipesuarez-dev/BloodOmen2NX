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
//   S: kain/walk_backwards root speed (62.5 u/s) * the clip node speed 1.7 of
//     the `animatio` graph. The graph is not decoded; the node is located by
//     its fields (in 0, out 1.5667 = the clip duration, speed 1.7, reel 3) at
//     offset 0x5504 of the `kain` entry, and the oracle confirms 106.25.
//     See docs/GAMEPLAY.md.
// Partial stick (slot 1 of FUN_00445ba0 is an unidentified clip) is not ported.
constexpr float kWalkBackClipSpeed = 1.7f;  // animatio clip node, "speed=%f" in FUN_005e1c20
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
    : collision_(collision), tuning_(tuning), jump_(derive_jump(tuning)) {
    if (!(tuning.stick_deadzone >= 0.0f && tuning.stick_deadzone < 1.0f) ||
        !(tuning.jump.terminal_fall > 0.0f)) {
        throw std::invalid_argument("kain tuning has an invalid deadzone or fall speed cap");
    }
    const auto positive = [](float value) { return std::isfinite(value) && value > 0.0f; };
    if (!positive(tuning.run_clip_rate) || !positive(tuning.run_root_speed) ||
        !positive(tuning.walk_back_root_speed)) {
        throw std::invalid_argument("kain tuning has an invalid run rate or root speed");
    }
    run_speed_ = tuning.run_root_speed * tuning.run_clip_rate;
    back_speed_ = tuning.walk_back_root_speed * kWalkBackClipSpeed;
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
    if (input.move_x != 0.0f && deadzone <= std::fabs(input.move_x)) {  // FUN_0041ecf0: `deadzone <= ax`
        const float scale =
            std::min(std::fabs(input.move_x) / (1.0f - deadzone), kMaxTurnScale);
        const float direction = input.move_x > 0.0f ? -1.0f : 1.0f;  // ax > 0 lowers the yaw
        state_.yaw += direction * scale * turn_rate * dt;
    }
    state_.yaw -= input.look_x * kLookSpeed * dt;
    state_.pitch += input.look_y * kLookSpeed * dt;
    state_.pitch = std::clamp(state_.pitch, -1.2f, 1.2f);

    float speed = 0.0f;
    if (forward_held) {
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
    const float travel = std::fabs(speed) * dt;
    if (travel > 0.0f) {
        // The original body sphere is small next to a tick of travel, so walk
        // the move in sub-steps of at most one radius; otherwise a thin wall
        // can be skipped between two samples.
        const int steps = std::clamp(static_cast<int>(std::ceil(travel / kBodyRadius)), 1, kMaxSubSteps);
        const bo2::engine::Vec3 sub_step = heading * (speed * dt / static_cast<float>(steps));
        for (int index = 0; index < steps; ++index) {
            const bo2::engine::Vec3 next = state_.position + sub_step;
            const bo2::engine::Vec3 body_center =
                next + bo2::engine::Vec3{0.0f, 0.0f, kBodyRadius + kBodyClearance};
            if (collision_.intersects_sphere(body_center, kBodyRadius)) {
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
        state_.vertical_velocity = jump_.launch_speed;
        state_.grounded = false;
        air_time_ = 0.0f;
        boost_armed_ = true;
    }
    jump_was_down_ = input.jump;

    // Gravity is g_up while vz >= 0 and g_down once vz < 0 (FUN_00442ae0). A
    // held jump adds boost_accel between boost_start and boost_end after
    // takeoff, averaged over the part of the tick that lies in the window.
    float acceleration =
        state_.vertical_velocity >= 0.0f ? tuning_.jump.gravity_up : tuning_.jump.gravity_down;
    if (boost_armed_) {
        acceleration += jump_.boost_accel *
                        window_overlap(air_time_, dt, jump_.boost_start, jump_.boost_end) / dt;
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
    } else {
        state_.grounded = false;
        air_time_ += dt;
    }
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
