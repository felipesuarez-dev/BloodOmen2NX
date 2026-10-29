#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "bo2/engine/cell_group.hpp"
#include "bo2/engine/collision.hpp"
#include "bo2/engine/world.hpp"
#include "bo2/game/player.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

// Flat floor at z = 0. Kain covers about 200 units per second, so the
// default is much wider than the old placeholder floor.
bo2::engine::LevelGeometry make_floor(float half_size = 2000.0f) {
    bo2::engine::LevelGeometry geometry;
    geometry.positions = {{-half_size, -half_size, 0.0f}, {half_size, -half_size, 0.0f},
                          {half_size, half_size, 0.0f}, {-half_size, half_size, 0.0f}};
    geometry.triangles = {{0, 1, 2, 0, 0, 0xFFFFFFFFU, -1, -1},
                          {0, 2, 3, 0, 0, 0xFFFFFFFFU, -1, -1}};
    return geometry;
}

// A wall across the x axis at x = wall_x, standing on the floor.
void add_wall(bo2::engine::LevelGeometry& geometry, float wall_x) {
    const std::uint32_t base = static_cast<std::uint32_t>(geometry.positions.size());
    geometry.positions.push_back({wall_x, -500.0f, 0.0f});
    geometry.positions.push_back({wall_x, 500.0f, 0.0f});
    geometry.positions.push_back({wall_x, 500.0f, 300.0f});
    geometry.positions.push_back({wall_x, -500.0f, 300.0f});
    geometry.triangles.push_back({base, base + 1, base + 2, 0, 0, 0xFFFFFFFFU, -1, -1});
    geometry.triangles.push_back({base, base + 2, base + 3, 0, 0, 0xFFFFFFFFU, -1, -1});
}

// The values the real kain tunedata holds (docs/FORMATS/tunedata.md), except
// the turn rates: Kain's are all 2.5, these are distinct so a swap between
// Ready, Run and WalkBack fails a test.
constexpr float kTurnReady = 2.0f;
constexpr float kTurnRun = 2.6f;
constexpr float kTurnWalkBack = 3.1f;
// Root speeds of kain/run and kain/walk_backwards (docs/FORMATS/animreel.md).
constexpr float kRunRootSpeed = 200.0f;
constexpr float kWalkBackRootSpeed = 62.5f;
constexpr float kWalkBackClipSpeed = 1.7f;  // kain animatio graph, walk_backwards clip node

bo2::game::KainTuning kain_tuning() {
    bo2::game::KainTuning tuning;
    tuning.stick_deadzone = 0.1f;
    tuning.jump = {-2500.0f, -700.0f, -128.0f, 512.0f, 160.0f, 64.0f, 128.0f, 0.066f, 0.3f};
    // The air control fields of the real tunedata (B+0x9DC, B+0xA50, B+0xA20, B+0xA38, B+0xA74).
    tuning.jump_turn_rate = 45.0f;
    tuning.side_jump = {-3000.0f, -500.0f, -96.0f, 512.0f, 160.0f, 64.0f, 64.0f, 0.05f, 0.06f};
    tuning.air_forward = {96.0f, 320.0f, 0.03f, 0.07f, 96.0f, 0.0f};
    tuning.air_side = {72.0f, 192.0f, 0.066f, 0.25f, 96.0f, 256.0f};
    tuning.air_back = {64.0f, 192.0f, 0.066f, 0.25f, 96.0f, 0.0f};
    tuning.turn_rate_ready = kTurnReady;
    tuning.turn_rate_run = kTurnRun;
    tuning.turn_rate_walk_back = kTurnWalkBack;
    tuning.run_clip_rate = 1.15f;
    tuning.run_root_speed = kRunRootSpeed;
    tuning.walk_back_root_speed = kWalkBackRootSpeed;
    tuning.walk_back_clip_speed = kWalkBackClipSpeed;
    return tuning;
}

constexpr double kTick = 1.0 / 60.0;

void run(bo2::game::PlayerController& player, const bo2::game::PlayerInput& input, int ticks) {
    for (int tick = 0; tick < ticks; ++tick) {
        player.step(input, kTick);
    }
}

struct Flight {
    float peak = 0.0f;
    float min_velocity = 0.0f;
    int ticks = 0;
    bool landed = false;
};

// Steps until the player is airborne and lands again. `hold_ticks` is how many
// ticks the jump button stays down from the first one.
Flight fly(bo2::game::PlayerController& player, int hold_ticks) {
    Flight flight;
    bo2::game::PlayerInput input;
    run(player, input, 2);  // settle on the floor
    for (int tick = 0; tick < 600; ++tick) {
        input.jump = tick < hold_ticks;
        player.step(input, kTick);
        ++flight.ticks;
        flight.peak = std::max(flight.peak, player.state().position.z);
        flight.min_velocity = std::min(flight.min_velocity, player.state().vertical_velocity);
        if (player.state().grounded) {
            flight.landed = true;
            break;
        }
    }
    return flight;
}

// Yaw after one second with the given stick x (and optional forward axis).
float yaw_after_one_second(float move_x, float move_y) {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    bo2::game::PlayerInput input;
    input.move_x = move_x;
    input.move_y = move_y;
    run(player, input, 60);
    return player.state().yaw;
}

void test_turn_rate() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    bo2::game::PlayerInput input;
    // A is a negative stick x and raises the yaw: rate / (1 - 0.1) rad/s
    // (FUN_0041ecf0), here the Ready rate.
    input.move_x = -1.0f;
    run(player, input, 60);
    CHECK(std::fabs(player.state().yaw - kTurnReady / 0.9f) < 1.0e-3f);
    // D is a positive stick x and turns back.
    input.move_x = 1.0f;
    run(player, input, 60);
    CHECK(std::fabs(player.state().yaw) < 1.0e-3f);
    // Turning is in place: no strafe.
    CHECK(std::fabs(player.state().position.x) < 1.0e-5f);
    CHECK(std::fabs(player.state().position.y) < 1.0e-5f);
}

void test_turn_rate_follows_the_state() {
    // Ready while idle, Run while walking forward, WalkBack while walking back.
    CHECK(std::fabs(yaw_after_one_second(-1.0f, 0.0f) - kTurnReady / 0.9f) < 1.0e-3f);
    CHECK(std::fabs(yaw_after_one_second(-1.0f, 1.0f) - kTurnRun / 0.9f) < 1.0e-3f);
    CHECK(std::fabs(yaw_after_one_second(-1.0f, -1.0f) - kTurnWalkBack / 0.9f) < 1.0e-3f);
}

void test_analog_turn_and_deadzone() {
    // |ax| = 0.5 scales the rate by 0.5 / (1 - 0.1).
    CHECK(std::fabs(yaw_after_one_second(-0.5f, 0.0f) - kTurnReady * 0.5f / 0.9f) < 1.0e-3f);
    CHECK(std::fabs(yaw_after_one_second(0.5f, 0.0f) + kTurnReady * 0.5f / 0.9f) < 1.0e-3f);
    // Inside the deadzone nothing turns; FUN_0041ecf0 turns from `deadzone <= ax`.
    CHECK(yaw_after_one_second(0.05f, 0.0f) == 0.0f);
    CHECK(yaw_after_one_second(-0.05f, 0.0f) == 0.0f);
    CHECK(yaw_after_one_second(-0.1f, 0.0f) > 0.0f);
    CHECK(yaw_after_one_second(0.1f, 0.0f) < 0.0f);
}

void test_forward_follows_the_yaw() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    bo2::game::PlayerState turned;
    turned.yaw = 1.5707963f;
    player.restore(turned);
    bo2::game::PlayerInput input;
    input.move_y = 1.0f;
    run(player, input, 60);
    CHECK(std::fabs(player.state().position.x) < 1.0e-2f);
    CHECK(player.state().position.y > 150.0f);
}

void test_forward_speed() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    bo2::game::PlayerInput input;
    input.move_y = 1.0f;
    run(player, input, 60);
    // 100 u/s for 0.18 s (measured), then run root speed * run clip rate
    // (FUN_00445ba0), which is 230 u/s.
    const float run_speed = kRunRootSpeed * 1.15f;
    CHECK(std::fabs(run_speed - 230.0f) < 1.0e-3f);
    const float expected = 0.18f * 100.0f + 0.82f * run_speed;
    CHECK(std::fabs(player.state().position.x - expected) < 2.5f);
    CHECK(std::fabs(player.state().position.y) < 1.0e-3f);
    CHECK(std::fabs(player.state().position.z) < 1.0e-4f);
    CHECK(player.state().grounded);
    // Released input stops at once.
    const float stopped = player.state().position.x;
    input.move_y = 0.0f;
    run(player, input, 30);
    CHECK(player.state().position.x == stopped);
}

void test_shift_speed() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    bo2::game::PlayerInput input;
    input.move_y = 1.0f;
    input.run = true;
    run(player, input, 60);
    CHECK(std::fabs(player.state().position.x - 200.0f) < 0.5f);
}

void test_backward_speed() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    bo2::game::PlayerInput input;
    input.move_y = -1.0f;
    run(player, input, 60);
    const float back_speed = kWalkBackRootSpeed * kWalkBackClipSpeed;
    CHECK(std::fabs(back_speed - 106.25f) < 1.0e-3f);
    CHECK(std::fabs(player.state().position.x + back_speed) < 0.5f);
    CHECK(player.state().yaw == 0.0f);
}

void test_speeds_follow_the_tuning() {
    // Non-default root speeds and clip rate: a hard-coded 230 / 106.25 fails.
    bo2::game::KainTuning tuning = kain_tuning();
    tuning.run_root_speed = 150.0f;
    tuning.run_clip_rate = 2.0f;
    tuning.walk_back_root_speed = 40.0f;
    tuning.walk_back_clip_speed = 2.5f;
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, tuning);
    bo2::game::PlayerInput input;
    input.move_y = 1.0f;
    run(player, input, 30);  // past the 0.18 s start phase
    const float before = player.state().position.x;
    run(player, input, 60);
    CHECK(std::fabs(player.state().position.x - before - 300.0f) < 0.5f);

    bo2::game::PlayerController back(collision, {0.0f, 0.0f, 0.0f}, tuning);
    input.move_y = -1.0f;
    run(back, input, 60);
    CHECK(std::fabs(back.state().position.x + 40.0f * 2.5f) < 0.5f);
}

void test_wall_blocks_the_walk() {
    bo2::engine::LevelGeometry level = make_floor();
    add_wall(level, 100.0f);
    bo2::engine::CollisionWorld collision(level);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    bo2::game::PlayerInput input;
    input.move_y = 1.0f;
    run(player, input, 120);
    CHECK(player.state().position.x < 100.0f);
    CHECK(player.state().position.x > 90.0f);
}

void test_tap_jump() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    const Flight flight = fly(player, 1);
    CHECK(flight.landed);
    CHECK(std::fabs(flight.peak - 64.0f) < 0.5f);
    // 0.2263 s up plus 0.4276 s down; the landing is seen on the next tick.
    const double airtime = static_cast<double>(flight.ticks) * kTick;
    CHECK(std::fabs(airtime - 0.654) < 0.03);
    CHECK(std::fabs(player.state().position.z) < 1.0e-4f);
    CHECK(player.state().vertical_velocity == 0.0f);
}

void test_held_jump() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    const Flight flight = fly(player, 1000);
    CHECK(flight.landed);
    CHECK(flight.peak > 120.0f);
    CHECK(flight.peak <= 128.05f);
    // Longer than a tap: the apex is higher and the fall is slower.
    CHECK(static_cast<double>(flight.ticks) * kTick > 0.9);
}

// Closed-form apex when the boost adds `boost` to g_up from `begin` to `end`
// seconds after takeoff: z(t) = v0 t + g t^2 / 2 + dv (t - (begin + end) / 2).
double apex_with_boost(double begin, double end) {
    const bo2::game::KainTuning tuning = kain_tuning();
    const bo2::game::JumpDerived jump = bo2::game::derive_jump(tuning);
    const double gravity = tuning.jump.gravity_up;
    const double launch = jump.launch_speed;
    const double impulse = static_cast<double>(jump.boost_accel) * (end - begin);
    const double t_apex = (launch + impulse) / -gravity;
    return launch * t_apex + 0.5 * gravity * t_apex * t_apex +
           impulse * (t_apex - 0.5 * (begin + end));
}

void test_early_release_gives_a_partial_boost() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    // Down for ticks 0-4, up on tick 5. FUN_00443a50 adds the tick's boost
    // before it looks at the button, so tick 5 is still boosted and the
    // boost ends at 6 ticks (0.1 s), not at 5.
    const double begin = 0.066;
    const double expected = apex_with_boost(begin, 6.0 * kTick);
    const double old_ordering = apex_with_boost(begin, 5.0 * kTick);
    const Flight flight = fly(player, 5);
    CHECK(std::fabs(static_cast<double>(flight.peak) - expected) < 0.3);
    // The two orderings are far enough apart for the check above to tell them apart.
    CHECK(std::fabs(expected - old_ordering) > 3.0);
    CHECK(flight.peak > 64.0f);
    CHECK(flight.peak < 128.0f);
}

void test_jump_fires_on_the_press_edge() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    bo2::game::PlayerInput input;
    run(player, input, 2);
    // Hold the button through the landing: one jump only.
    input.jump = true;
    int launches = 0;
    for (int tick = 0; tick < 240; ++tick) {
        const bool was_grounded = player.state().grounded;
        player.step(input, kTick);
        if (was_grounded && !player.state().grounded) {
            ++launches;
        }
    }
    CHECK(launches == 1);
    CHECK(player.state().grounded);
    CHECK(std::fabs(player.state().position.z) < 1.0e-4f);
    // Releasing and pressing again jumps again.
    input.jump = false;
    player.step(input, kTick);
    input.jump = true;
    player.step(input, kTick);
    CHECK(!player.state().grounded);
    CHECK(player.state().vertical_velocity > 0.0f);
}

// Launch speeds, accelerations and peaks that FUN_00452030 derives from the
// real tunedata (docs/GAMEPLAY.md, "Air"). The forward peak is what the Wine
// oracle measured as 323.0 u/s in the air.
constexpr float kForwardLaunch = 146.813f;
constexpr float kForwardAccel = 4403.56f;
constexpr float kForwardPeak = 322.956f;
constexpr float kBackLaunch = 97.8755f;
constexpr float kBackAccel = 583.535f;
constexpr float kBackPeak = 205.246f;
constexpr float kSideLaunch = 110.110f;
constexpr float kSideAccel = 504.829f;
constexpr float kSidePeak = 202.998f;
constexpr float kDegreesToRadians = 0.017453292519943295f;

bo2::game::PlayerInput jump_input(float move_x, float move_y) {
    bo2::game::PlayerInput input;
    input.move_x = move_x;
    input.move_y = move_y;
    input.jump = true;
    return input;
}

// The jump velocity in Kain's frame: x along his heading, y to his left.
bo2::engine::Vec2 local_air_velocity(const bo2::game::PlayerController& player) {
    const bo2::engine::Vec2 velocity = player.air_velocity();
    const float yaw = player.state().yaw;
    return {velocity.x * std::cos(yaw) + velocity.y * std::sin(yaw),
            -velocity.x * std::sin(yaw) + velocity.y * std::cos(yaw)};
}

void test_forward_jump_ramps_to_the_peak_speed() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    run(player, {}, 2);
    const bo2::game::PlayerInput input = jump_input(0.0f, 1.0f);
    player.step(input, kTick);
    // FUN_004436e0: |ay| * the launch speed of the forward block, along the yaw.
    CHECK(!player.state().grounded);
    CHECK(std::fabs(player.air_velocity().x - kForwardLaunch) < 0.01f);
    CHECK(std::fabs(player.air_velocity().y) < 1.0e-3f);
    // FUN_004423b0 adds accel * dt per tick toward the stick target (the peak).
    player.step(input, kTick);
    CHECK(std::fabs(player.air_velocity().x - (kForwardLaunch + kForwardAccel / 60.0f)) < 0.05f);
    run(player, input, 4);
    CHECK(std::fabs(player.air_velocity().x - kForwardPeak) < 0.01f);
    // It stays at the peak for the rest of the flight.
    run(player, input, 10);
    CHECK(std::fabs(player.air_velocity().x - kForwardPeak) < 0.01f);
}

void test_released_stick_damps_the_air_speed() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    run(player, {}, 2);
    bo2::game::PlayerInput input = jump_input(0.0f, 1.0f);
    run(player, input, 8);
    input.move_y = 0.0f;
    // With no stick the velocity keeps 96 percent per tick and loses the back
    // acceleration times dt (FUN_004423b0, target zero).
    player.step(input, kTick);
    CHECK(std::fabs(player.air_velocity().x - (kForwardPeak * 0.96f - kBackAccel / 60.0f)) < 0.05f);
    // Then it falls to zero and stays there, never negative.
    for (int tick = 0; tick < 60; ++tick) {
        player.step(input, kTick);
        CHECK(player.air_velocity().x >= 0.0f);
    }
    CHECK(player.air_velocity().x == 0.0f);
}

void test_back_jump() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    run(player, {}, 2);
    const bo2::game::PlayerInput input = jump_input(0.0f, -1.0f);
    player.step(input, kTick);
    CHECK(std::fabs(player.air_velocity().x + kBackLaunch) < 0.01f);
    player.step(input, kTick);
    CHECK(std::fabs(player.air_velocity().x + kBackLaunch + kBackAccel / 60.0f) < 0.05f);
    run(player, input, 20);
    CHECK(std::fabs(player.air_velocity().x + kBackPeak) < 0.01f);
    // The yaw is unchanged: a back jump keeps facing forward.
    CHECK(player.state().yaw == 0.0f);
}

void test_sideways_jump_uses_the_side_model() {
    // D (positive stick x) is kind 4 and goes to Kain's right, A is kind 3 and
    // goes left; the left of yaw 0 is +y.
    for (const float stick : {1.0f, -1.0f}) {
        const bo2::engine::LevelGeometry floor = make_floor();
        bo2::engine::CollisionWorld collision(floor);
        bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
        run(player, {}, 2);
        const bo2::game::PlayerInput input = jump_input(stick, 0.0f);
        player.step(input, kTick);
        // The takeoff tick still turns Kain on the ground; the velocity is
        // rotated by that yaw, so read it in Kain's frame.
        const float yaw_after_takeoff = player.state().yaw;
        const float direction = stick > 0.0f ? -1.0f : 1.0f;
        CHECK(std::fabs(local_air_velocity(player).y - direction * kSideLaunch) < 0.01f);
        CHECK(std::fabs(local_air_velocity(player).x) < 1.0e-3f);
        // The side model: launch 619.68 at g -3000 (sqrt(2 * 64 * 3000)), no boost.
        CHECK(std::fabs(player.state().vertical_velocity - (619.677f - 3000.0f / 60.0f)) < 0.05f);
        run(player, input, 30);
        CHECK(std::fabs(local_air_velocity(player).y - direction * kSidePeak) < 0.01f);
        // A sideways jump neither turns nor moves along the yaw.
        CHECK(player.state().yaw == yaw_after_takeoff);
        CHECK(std::fabs(local_air_velocity(player).x) < 1.0e-3f);
    }
}

void test_sideways_jump_flight() {
    // Apex 64 from the side model, then the normal g_down (-700): 0.2066 s up
    // and 0.4276 s down, a bit shorter than the 0.654 s of a plain tap jump.
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    run(player, {}, 2);
    bo2::game::PlayerInput input = jump_input(1.0f, 0.0f);
    float peak = 0.0f;
    int ticks = 0;
    for (; ticks < 200; ++ticks) {
        player.step(input, kTick);
        input.jump = ticks < 1;  // a tap, though the side model has no boost
        peak = std::max(peak, player.state().position.z);
        if (player.state().grounded) {
            break;
        }
    }
    CHECK(std::fabs(peak - 64.0f) < 1.0f);
    CHECK(std::fabs(static_cast<double>(ticks + 1) * kTick - 0.634) < 0.03);
    CHECK(player.air_velocity().x == 0.0f && player.air_velocity().y == 0.0f);
}

void test_diagonal_stick_is_a_forward_jump() {
    // W plus D: |ax| <= 2 |ay|, so the jump is forward (FUN_00425580).
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    run(player, {}, 2);
    const bo2::game::PlayerInput input = jump_input(1.0f, 1.0f);
    player.step(input, kTick);
    CHECK(std::fabs(std::hypot(player.air_velocity().x, player.air_velocity().y) - kForwardLaunch) <
          0.01f);
    // A stick mostly sideways (|ax| > 2 |ay|) is a sideways jump instead.
    bo2::game::PlayerController side(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    run(side, {}, 2);
    side.step(jump_input(1.0f, 0.4f), kTick);
    CHECK(std::fabs(local_air_velocity(side).y + kSideLaunch) < 0.01f);
    CHECK(std::fabs(local_air_velocity(side).x) < 1.0e-3f);
    // Inside the deadzone the jump has no direction.
    bo2::game::PlayerController still(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    run(still, {}, 2);
    still.step(jump_input(0.05f, 0.05f), kTick);
    CHECK(still.air_velocity().x == 0.0f && still.air_velocity().y == 0.0f);
}

void test_air_turn_carries_the_velocity() {
    // A forward jump turns at jump_turn_rate deg/s (B+0x9DC): D lowers the yaw.
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    run(player, {}, 2);
    const bo2::game::PlayerInput input = jump_input(1.0f, 1.0f);
    player.step(input, kTick);
    const float yaw_after_takeoff = player.state().yaw;
    run(player, input, 20);
    const float expected = -45.0f * kDegreesToRadians * 20.0f / 60.0f;
    CHECK(std::fabs((player.state().yaw - yaw_after_takeoff) - expected) < 1.0e-4f);
    // The velocity turns with Kain: it points along the heading.
    const bo2::engine::Vec2 velocity = player.air_velocity();
    CHECK(std::fabs(std::atan2(velocity.y, velocity.x) - player.state().yaw) < 1.0e-3f);
    CHECK(std::fabs(std::hypot(velocity.x, velocity.y) - kForwardPeak) < 0.05f);
}

void test_air_turn_is_skipped_by_a_long_tick() {
    // FUN_00441f20 leaves the velocity alone on a tick that turns by a degree
    // or more: 45 deg/s * 0.05 s is 2.25 deg.
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    run(player, {}, 2);
    const bo2::game::PlayerInput input = jump_input(1.0f, 1.0f);
    player.step(input, 0.05);
    const float yaw = player.state().yaw;
    for (int tick = 0; tick < 3; ++tick) {
        player.step(input, 0.05);
    }
    CHECK(std::fabs((player.state().yaw - yaw) + 3.0f * 45.0f * kDegreesToRadians * 0.05f) < 1.0e-4f);
    CHECK(std::fabs(std::hypot(player.air_velocity().x, player.air_velocity().y) - kForwardLaunch) <
          0.01f);
}

void test_standing_jump_takes_a_direction_in_the_air() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    run(player, {}, 2);
    bo2::game::PlayerInput input = jump_input(0.0f, 0.0f);
    player.step(input, kTick);
    run(player, input, 3);
    CHECK(player.air_velocity().x == 0.0f && player.air_velocity().y == 0.0f);
    // D in the air installs the sideways kind: it speeds up to the side peak and does not turn.
    input.move_x = 1.0f;
    run(player, input, 30);
    CHECK(std::fabs(player.air_velocity().y + kSidePeak) < 0.01f);
    CHECK(player.state().yaw == 0.0f);

    // W in the air installs the forward kind and the forward peak.
    bo2::game::PlayerController forward(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    run(forward, {}, 2);
    input = jump_input(0.0f, 0.0f);
    forward.step(input, kTick);
    run(forward, input, 3);
    input.move_y = 1.0f;
    run(forward, input, 10);
    CHECK(std::fabs(forward.air_velocity().x - kForwardPeak) < 0.01f);
    // The kind is fixed for the flight: S now only slows it (back accel), it
    // does not turn into a back jump with its own peak.
    input.move_y = -1.0f;
    run(forward, input, 2);
    CHECK(forward.air_velocity().x < kForwardPeak);
    CHECK(forward.air_velocity().x > 0.0f);
}

void test_landing_ends_the_jump_velocity() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    run(player, {}, 2);
    bo2::game::PlayerInput input = jump_input(0.0f, 1.0f);
    for (int tick = 0; tick < 300 && !(tick > 2 && player.state().grounded); ++tick) {
        player.step(input, kTick);
        input.jump = false;
    }
    CHECK(player.state().grounded);
    CHECK(player.air_velocity().x == 0.0f && player.air_velocity().y == 0.0f);
    // A standing jump lands into the 100 u/s start phase of W.
    const float before = player.state().position.x;
    player.step(input, kTick);
    CHECK(std::fabs(player.state().position.x - before - 100.0f / 60.0f) < 1.0e-3f);
}

void test_running_jump_lands_at_run_speed() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    bo2::game::PlayerInput input;
    input.move_y = 1.0f;
    run(player, input, 30);  // past the 0.18 s start phase
    input.jump = true;
    for (int tick = 0; tick < 300 && !(tick > 2 && player.state().grounded); ++tick) {
        player.step(input, kTick);
        input.jump = false;
    }
    CHECK(player.state().grounded);
    // No start phase after a run landing: straight back to 200 * 1.15 u/s.
    const float before = player.state().position.x;
    player.step(input, kTick);
    CHECK(std::fabs(player.state().position.x - before - 230.0f / 60.0f) < 1.0e-3f);
}

void test_wall_stops_a_jump() {
    bo2::engine::LevelGeometry level = make_floor();
    add_wall(level, 60.0f);
    bo2::engine::CollisionWorld collision(level);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    run(player, {}, 2);
    bo2::game::PlayerInput input = jump_input(0.0f, 1.0f);
    for (int tick = 0; tick < 120; ++tick) {
        player.step(input, kTick);
        input.jump = false;
    }
    CHECK(player.state().position.x < 60.0f);
    CHECK(player.state().position.x > 40.0f);
    CHECK(player.state().grounded);
}

void test_a_fall_keeps_the_ground_speed() {
    // Walking off a ledge is not a jump: the port keeps the ground speed of the
    // held input (docs/GAMEPLAY.md, "Ledge fall").
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 300.0f}, kain_tuning());
    bo2::game::PlayerInput input;
    input.move_y = 1.0f;
    run(player, input, 6);
    CHECK(!player.state().grounded);
    CHECK(std::fabs(player.state().position.x - 10.0f) < 0.1f);
    CHECK(player.air_velocity().x == 0.0f);
}

void test_restore_clears_the_jump() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    run(player, {}, 2);
    run(player, jump_input(0.0f, 1.0f), 3);
    CHECK(player.air_velocity().x > 0.0f);
    player.restore(bo2::game::PlayerState{});
    CHECK(player.air_velocity().x == 0.0f && player.air_velocity().y == 0.0f);
}

void test_huge_step_is_capped() {
    // A stalled frame must not throw Kain through a wall or the floor.
    bo2::engine::LevelGeometry level = make_floor();
    add_wall(level, 5.0f);
    bo2::engine::CollisionWorld collision(level);
    bo2::game::PlayerController walker(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    bo2::game::PlayerInput input;
    input.move_y = 1.0f;
    walker.step(input, 1.0e9);
    CHECK(walker.state().position.x > 3.0f);
    CHECK(walker.state().position.x < 5.0f);

    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld open(floor);
    bo2::game::PlayerController runner(open, {0.0f, 0.0f, 0.0f}, kain_tuning());
    runner.step(input, 1.0e30);
    // Capped at 0.1 s of the 100 u/s start speed.
    CHECK(std::fabs(runner.state().position.x - 10.0f) < 0.1f);

    bo2::game::PlayerController faller(open, {0.0f, 0.0f, 3.0f}, kain_tuning());
    faller.step({}, 1.0e9);
    CHECK(faller.state().grounded);
    CHECK(std::fabs(faller.state().position.z) < 1.0e-4f);

    // Non-finite steps are ignored.
    bo2::game::PlayerController still(open, {0.0f, 0.0f, 0.0f}, kain_tuning());
    still.step(input, std::numeric_limits<double>::infinity());
    still.step(input, std::numeric_limits<double>::quiet_NaN());
    CHECK(still.state().position.x == 0.0f);
}

void test_terminal_fall_speed() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 3000.0f}, kain_tuning());
    const Flight flight = fly(player, 0);
    CHECK(flight.landed);
    CHECK(flight.min_velocity >= -512.0f - 1.0e-3f);
    CHECK(flight.min_velocity <= -511.0f);
    // The fast fall lands on the floor instead of passing through it.
    CHECK(std::fabs(player.state().position.z) < 1.0e-4f);
}

void test_invalid_tuning_is_rejected() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bool threw = false;
    try {
        bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f},
                                           bo2::game::KainTuning{});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    // Root speeds must be positive and finite.
    const float bad_speeds[] = {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
                                std::numeric_limits<float>::quiet_NaN()};
    for (const float bad : bad_speeds) {
        for (int which = 0; which < 3; ++which) {
            bo2::game::KainTuning tuning = kain_tuning();
            float* const field[] = {&tuning.run_root_speed, &tuning.walk_back_root_speed,
                                    &tuning.walk_back_clip_speed};
            *field[which] = bad;
            threw = false;
            try {
                bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, tuning);
            } catch (const std::invalid_argument&) {
                threw = true;
            }
            CHECK(threw);
        }
    }
}

void test_attack_and_damage() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    bo2::engine::EntityWorld world;
    const auto target = world.add({2.0f, 0.0f, 1.0f}, 0.5f, 0);
    std::vector<std::uint64_t> targets;
    CHECK(player.attack(targets, world));
    CHECK(targets.size() == 1);
    CHECK(world.damage(target, 25.0f));
    CHECK(world.entities()[0].health == 75.0f);
    bo2::game::PlayerInput input;
    input.attack = true;
    player.step(input, 0.016);
    CHECK(player.state().attack_cooldown > 0.0f);
    const std::uint32_t combo = player.state().combo;
    CHECK(combo == 1);
}

void test_restore() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    bo2::game::PlayerState target;
    target.position = {3.0f, 4.0f, 0.0f};
    target.yaw = 1.0f;
    player.restore(target);
    CHECK(player.state().position.x == 3.0f);
    CHECK(player.state().yaw == 1.0f);
    CHECK(player.state().vertical_velocity == 0.0f);
}

void test_camera_look() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f}, kain_tuning());
    bo2::game::PlayerInput input;
    input.look_x = 1.0f;
    player.step(input, 0.1);
    CHECK(player.state().yaw < 0.0f);
    CHECK(std::fabs(player.camera_eye().z - 1.7f) < 1.0e-5f);
}

}  // namespace

int main() {
    test_turn_rate();
    test_turn_rate_follows_the_state();
    test_analog_turn_and_deadzone();
    test_forward_follows_the_yaw();
    test_forward_speed();
    test_shift_speed();
    test_backward_speed();
    test_speeds_follow_the_tuning();
    test_wall_blocks_the_walk();
    test_tap_jump();
    test_held_jump();
    test_early_release_gives_a_partial_boost();
    test_jump_fires_on_the_press_edge();
    test_forward_jump_ramps_to_the_peak_speed();
    test_released_stick_damps_the_air_speed();
    test_back_jump();
    test_sideways_jump_uses_the_side_model();
    test_sideways_jump_flight();
    test_diagonal_stick_is_a_forward_jump();
    test_air_turn_carries_the_velocity();
    test_air_turn_is_skipped_by_a_long_tick();
    test_standing_jump_takes_a_direction_in_the_air();
    test_landing_ends_the_jump_velocity();
    test_running_jump_lands_at_run_speed();
    test_wall_stops_a_jump();
    test_a_fall_keeps_the_ground_speed();
    test_restore_clears_the_jump();
    test_huge_step_is_capped();
    test_terminal_fall_speed();
    test_invalid_tuning_is_rejected();
    test_attack_and_damage();
    test_restore();
    test_camera_look();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "player tests: ok\n";
    return 0;
}
