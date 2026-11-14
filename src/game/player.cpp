#include "bo2/game/player.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bo2::game {

namespace {

// Ground movement is animation root motion in the original: FUN_0041f7d0 adds
// the root delta of FUN_0041fb00 to the position. `step` takes that delta as
// `motion`, which KainAnimation works out from the animation state
// machine (docs/FORMATS/animatio.md, Root motion). Without it the controller
// falls back to the steady speeds of the clips (docs/FORMATS/animreel.md):
//   W at full stick: kain/run root speed (200 u/s) * the run clip rate
//     A+0x40 (1.15), FUN_00445ba0 slot 0.
//   S: kain/walk_backwards root speed (62.5 u/s) * the speed of its clip node
//     in the `kain` animatio graph (1.7).
// The fallback has no start of the run and no pause before the walk back:
// those come from the blends of the graph.
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

// Provisional: these predate the Kain tuning, are in meter-like units (Kain's
// units are about 100x larger) and still have to be recovered.
constexpr float kEyeHeight = 1.7f;
// Run probe (FUN_0042ee60, traced live): a radius-8 sphere swept from the
// chest 32 u forward. On a terrain hit the original compares the hit normal
// with the tuning threshold (A+0x614, 0.9) and sets the wall flag
// (KainState+0x588 bit 0x100); it does not clamp the move. The port records
// the flag only: the wall-slide state that consumes it is not ported yet.
constexpr float kRunProbeRadius = 8.0f;
constexpr float kRunProbeLength = 32.0f;
constexpr float kRunProbeHeight = 48.0f;
constexpr float kRunProbeFacing = 0.9f;  // tunedata wallhug triggerDPThresh
constexpr float kLookSpeed = 2.0f;  // port debug aid (mouse and right stick look)
constexpr float kAttackCooldown = 0.45f;
constexpr float kAttackRange = 2.2f;
constexpr float kAttackRadius = 1.0f;

// Kain's body collision proxy is the chest sphere of his `collmodc`
// (kain.big), offset from the feet. The live dump of the movement proxy
// (`wine_shape2_20261113`) shows three spheres, but only the first one has
// the flag bit the body query passes (FUN_0062ef20 tests `flags & 1` against
// the manager's query flag 2; the other two spheres have flags 0). The model
// AABB at proxy+0x58 is only the broad-phase bound.
const std::vector<bo2::engine::BodySphere>& kain_body() {
    static const std::vector<bo2::engine::BodySphere> body = {
        {{0.0f, 0.0f, 51.98f}, 19.800f},
    };
    return body;
}

// Seconds of [begin, begin + dt) that fall inside [window_begin, window_end).
float window_overlap(float begin, float dt, float window_begin, float window_end) {
    const float low = std::max(begin, window_begin);
    const float high = std::min(begin + dt, window_end);
    return std::max(0.0f, high - low);
}

// FUN_00445ba0 at stick magnitude s: layer 0 (run) gets weight s and rate
// (0.5 s + 0.5) * run_clip_rate; layer 1 (walk) gets weight 1 - s and rate
// (s + 1) * run_clip_rate. The ground speed is the weighted sum of the two
// root motions (kain/run and kain/walk, docs/FORMATS/animreel.md).
float partial_forward_speed(float stick, float run_clip_rate, float run_root_speed,
                            float walk_root_speed) {
    const float s = std::clamp(stick, 0.0f, 1.0f);
    const float run_rate = (0.5f * s + 0.5f) * run_clip_rate;
    const float walk_rate = (s + 1.0f) * run_clip_rate;
    return run_rate * s * run_root_speed + walk_rate * (1.0f - s) * walk_root_speed;
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
        !positive(tuning.walk_root_speed) || !positive(tuning.walk_back_root_speed) ||
        !positive(tuning.walk_back_clip_speed)) {
        throw std::invalid_argument("kain tuning has an invalid run rate or root speed");
    }
    back_speed_ = tuning.walk_back_root_speed * tuning.walk_back_clip_speed;
    state_.position = start;
}

void PlayerController::step(const PlayerInput& input, double seconds,
                            const GroundMotion* motion) {
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
    } else if (motion != nullptr && !motion->turn) {
        // The combat movement strafes: no tank turn (FUN_0044a380).
    } else if (input.move_x != 0.0f && deadzone <= std::fabs(input.move_x)) {  // FUN_0041ecf0: `deadzone <= ax`
        const float scale =
            std::min(std::fabs(input.move_x) / (1.0f - deadzone), kMaxTurnScale);
        const float direction = input.move_x > 0.0f ? -1.0f : 1.0f;  // ax > 0 lowers the yaw
        state_.yaw += direction * scale * turn_rate * dt;
    }
    state_.yaw -= input.look_x * kLookSpeed * dt;
    state_.pitch += input.look_y * kLookSpeed * dt;
    state_.pitch = std::clamp(state_.pitch, -1.2f, 1.2f);

    // Ground speed. A jump has its own horizontal velocity (steer_in_air). On
    // the ground the animation moves Kain (FUN_0041f460): the caller hands in
    // the forward and lateral distance of this tick. A plain fall, and a caller without
    // an animation, keep the steady speed of the held input.
    float speed = 0.0f;
    const bool rooted = !air_jump_ && motion != nullptr && state_.grounded;
    if (air_jump_ || rooted) {
        // Horizontal motion comes from steer_in_air or from the root motion.
    } else if (forward_held) {
        const float stick =
            std::min(1.0f, std::sqrt(input.move_x * input.move_x + input.move_y * input.move_y));
        speed = partial_forward_speed(stick, tuning_.run_clip_rate, tuning_.run_root_speed,
                                      tuning_.walk_root_speed);
    } else if (back_held) {
        speed = -back_speed_;
    }

    const bo2::engine::Vec3 heading{std::cos(state_.yaw), std::sin(state_.yaw), 0.0f};
    // The step of this tick: the jump velocity, or the ground speed along the yaw.
    const bo2::engine::Vec3 left{-std::sin(state_.yaw), std::cos(state_.yaw), 0.0f};
    const bo2::engine::Vec3 displacement =
        air_jump_ ? bo2::engine::Vec3{air_velocity_.x * dt, air_velocity_.y * dt, 0.0f}
                  : (rooted ? heading * motion->forward + left * motion->lateral
                            : heading * (speed * dt));
    const float travel = bo2::engine::length(displacement);
    // FUN_0042ee60: the run probe is a sphere swept from the chest 32 u
    // forward (FUN_005be9a0) and it records a wall it faces (the hit normal
    // opposes the run by more than the tunedata 0.9 threshold). It never
    // clamps the move, and a live trace of the W run fills no blocker record
    // (FUN_0041ee60 is a no-op), so the port only keeps the flag here.
    wall_hug_ = false;
    if (travel > 0.0f && state_.grounded &&
        bo2::engine::dot(displacement, heading) > 0.0f) {
        const std::int32_t cell = collision_.cell_at(state_.position);
        const bo2::engine::Vec3 origin =
            state_.position + bo2::engine::Vec3{0.0f, 0.0f, kRunProbeHeight};
        const auto hit = collision_.swept_sphere(origin, heading * kRunProbeLength,
                                                 kRunProbeRadius, cell);
        if (hit.has_value() &&
            bo2::engine::dot(hit->normal, heading) < -kRunProbeFacing) {
            wall_hug_ = true;
            wall_normal_ = hit->normal;
        }
    }
    if (travel > 0.0f) {
        // FUN_005ccb60: the collision manager sweeps the body sphere,
        // advances to the earliest contact and slides the rest of the tick
        // along the surface. The ground snap owns z, so only the horizontal
        // part of the resolved position is applied here.
        // The original integrates at about 100 Hz, so a tick moves ~2.15 u
        // and follows a curved bevel closely. The replay rows are 20 ms
        // (4.6 u); splitting the move below that tick keeps the slide on the
        // same path instead of cutting the corner into the next surface.
        int steps = static_cast<int>(std::ceil(travel / 1.15f));
        steps = std::clamp(steps, 1, 8);
        const bo2::engine::Vec3 sub_step = displacement / static_cast<float>(steps);
        for (int index = 0; index < steps; ++index) {
            const bo2::engine::BodyMove move =
                collision_.slide_body(state_.position, kain_body(), sub_step,
                                      collision_.cell_at(state_.position));
            state_.position.x = move.position.x;
            state_.position.y = move.position.y;
            if (move.blocked) {
                // The original zeroes the forward or back velocity when a
                // probe is blocked (KainState+0x450/0x454); the port stops the jump.
                air_velocity_ = {0.0f, 0.0f};
            }
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
    // skip the floor. The original's ground/step probe is a radius-1 sphere
    // cast (FUN_00430820: `FUN_005be9a0` from `z+16` to `z-24`) whose hit is
    // applied as a 3D correction inside the slide state machine; porting the
    // vertical part alone drops the body into small pits, so the port keeps
    // the vertical raycast until that state is decoded.
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
