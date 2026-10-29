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

bo2::game::KainTuning kain_tuning() {
    bo2::game::KainTuning tuning;
    tuning.stick_deadzone = 0.1f;
    tuning.jump = {-2500.0f, -700.0f, -128.0f, 512.0f, 160.0f, 64.0f, 128.0f, 0.066f, 0.3f};
    tuning.turn_rate_ready = kTurnReady;
    tuning.turn_rate_run = kTurnRun;
    tuning.turn_rate_walk_back = kTurnWalkBack;
    tuning.run_clip_rate = 1.15f;
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
    // 100 u/s for 0.18 s, then 230 u/s (measured in the Wine oracle).
    const float expected = 0.18f * 100.0f + 0.82f * 230.0f;
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
    CHECK(std::fabs(player.state().position.x + 106.25f) < 0.5f);
    CHECK(player.state().yaw == 0.0f);
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
    test_wall_blocks_the_walk();
    test_tap_jump();
    test_held_jump();
    test_early_release_gives_a_partial_boost();
    test_jump_fires_on_the_press_edge();
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
