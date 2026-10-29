#include <algorithm>
#include <cmath>
#include <iostream>

#include "bo2/engine/cell_group.hpp"
#include "bo2/engine/collision.hpp"
#include "bo2/game/camera.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

constexpr double kTick = 0.02;  // the oracle game ticks at about 50 Hz
constexpr float kPi = 3.14159265358979f;

// A wall across the x axis at x = wall_x, tall enough to catch the camera pivot.
bo2::engine::LevelGeometry make_wall(float wall_x) {
    bo2::engine::LevelGeometry geometry;
    geometry.positions = {{wall_x, -500.0f, -1000.0f}, {wall_x, 500.0f, -1000.0f},
                          {wall_x, 500.0f, 1000.0f}, {wall_x, -500.0f, 1000.0f}};
    geometry.triangles = {{0, 1, 2, 0, 0, 0xFFFFFFFFU, -1, -1},
                          {0, 2, 3, 0, 0, 0xFFFFFFFFU, -1, -1}};
    return geometry;
}

bo2::engine::Vec3 view_direction(const bo2::game::FollowCamera& camera) {
    return bo2::engine::normalize(camera.target() - camera.eye());
}

void test_default_pose_matches_the_oracle() {
    const bo2::engine::LevelGeometry far_wall = make_wall(10000.0f);
    bo2::engine::CollisionWorld collision(far_wall);
    bo2::game::FollowCamera camera;
    bo2::game::PlayerState player;
    camera.update(player, collision);
    // The live setup: range 192 and pitch 6 degrees (0.10472 rad) from a pivot
    // 76 above Kain. The oracle read the eye 190.9 behind and 96.1 above him.
    CHECK(std::fabs(camera.eye().x + 190.9f) < 0.1f);
    CHECK(std::fabs(camera.eye().y) < 1.0e-3f);
    CHECK(std::fabs(camera.eye().z - 96.1f) < 0.1f);
    // The eye is exactly the range from the pivot.
    const bo2::engine::Vec3 pivot{0.0f, 0.0f, 76.0f};
    CHECK(std::fabs(bo2::engine::length(camera.eye() - pivot) - 192.0f) < 1.0e-3f);
    // Looking along the yaw, pitched down 6 degrees.
    const bo2::engine::Vec3 view = view_direction(camera);
    CHECK(std::fabs(view.z - std::sin(-0.10472f)) < 1.0e-4f);
    CHECK(view.x > 0.99f);
    CHECK(std::fabs(view.y) < 1.0e-4f);
}

void test_pose_follows_position_and_yaw() {
    const bo2::engine::LevelGeometry far_wall = make_wall(10000.0f);
    bo2::engine::CollisionWorld collision(far_wall);
    bo2::game::FollowCamera camera;
    bo2::game::PlayerState player;
    player.position = {100.0f, 50.0f, 20.0f};
    player.yaw = 1.5707963f;  // facing +y
    camera.update(player, collision);
    CHECK(std::fabs(camera.eye().x - 100.0f) < 1.0e-2f);
    CHECK(std::fabs(camera.eye().y - (50.0f - 190.9f)) < 0.1f);
    CHECK(std::fabs(camera.eye().z - (20.0f + 96.1f)) < 0.1f);
    const bo2::engine::Vec3 view = view_direction(camera);
    CHECK(view.y > 0.99f);
}

void test_wall_pulls_the_camera_in() {
    // Kain faces -x with a wall behind him at x = 100.
    const bo2::engine::LevelGeometry wall = make_wall(100.0f);
    bo2::engine::CollisionWorld collision(wall);
    bo2::game::FollowCamera camera;
    bo2::game::PlayerState player;
    player.yaw = 3.14159265f;
    camera.update(player, collision);
    CHECK(camera.eye().x < 100.0f);
    CHECK(camera.eye().x > 90.0f);
    // The view direction does not change when the eye is pulled in.
    CHECK(view_direction(camera).x < -0.99f);
}

// Camera yaw minus Kain's yaw, in degrees, wrapped to [-180, 180].
float lag_degrees(const bo2::game::FollowCamera& camera, const bo2::game::PlayerState& player) {
    return std::remainder(camera.yaw() - player.yaw, 2.0f * kPi) * 180.0f / kPi;
}

void test_yaw_stays_close_on_a_tank_turn() {
    // Kain turns at 160 deg/s (the oracle's A key) for a second. FUN_00407ec0
    // catches up inside a tick or two, so the camera never lags by more than a
    // couple of degrees; the oracle measured 0.2 degrees.
    const bo2::engine::LevelGeometry far_wall = make_wall(100000.0f);
    bo2::engine::CollisionWorld collision(far_wall);
    bo2::game::FollowCamera camera;
    bo2::game::PlayerState player;
    camera.update(player, collision);
    float worst = 0.0f;
    for (int tick = 0; tick < 50; ++tick) {
        player.yaw += 160.0f * kPi / 180.0f * static_cast<float>(kTick);
        camera.update(player, collision, kTick);
        worst = std::max(worst, std::fabs(lag_degrees(camera, player)));
    }
    CHECK(worst < 2.0f);
    CHECK(worst > 0.0f);  // it is a catch-up, not a rigid copy
    // Once Kain stops, the camera is on his yaw again within a few ticks.
    for (int tick = 0; tick < 5; ++tick) {
        camera.update(player, collision, kTick);
    }
    CHECK(std::fabs(lag_degrees(camera, player)) < 1.0e-3f);
}

void test_yaw_catch_up_is_limited_by_the_acceleration() {
    // A turn of 90 degrees at once cannot be followed at once: the yaw speed
    // changes by at most the limit per second, and the camera arrives.
    const bo2::engine::LevelGeometry far_wall = make_wall(100000.0f);
    bo2::engine::CollisionWorld collision(far_wall);
    bo2::game::FollowCamera camera;
    bo2::game::PlayerState player;
    camera.update(player, collision);
    player.yaw = kPi / 2.0f;
    float previous = 0.0f;
    float arrival = -1.0f;
    for (int tick = 1; tick <= 100; ++tick) {
        camera.update(player, collision, 0.005);
        const float yaw = camera.yaw();
        CHECK(yaw >= previous - 1.0e-6f);  // never overshoots backward
        CHECK(yaw <= player.yaw + 1.0e-4f);
        previous = yaw;
        if (arrival < 0.0f && std::fabs(yaw - player.yaw) < 1.0e-4f) {
            arrival = static_cast<float>(tick) * 0.005f;
        }
    }
    CHECK(arrival > 0.05f);   // not instant
    CHECK(arrival < 0.25f);   // sqrt(6 * 1.57 / 750) = 0.11 s with the limit of 750
    CHECK(std::fabs(lag_degrees(camera, player)) < 1.0e-3f);
}

void test_yaw_takes_the_short_way_round() {
    const bo2::engine::LevelGeometry far_wall = make_wall(100000.0f);
    bo2::engine::CollisionWorld collision(far_wall);
    bo2::game::FollowCamera camera;
    bo2::game::PlayerState player;
    player.yaw = 3.0f;
    camera.update(player, collision);
    player.yaw = -3.0f;  // 0.28 rad away through pi, not 6 rad the other way
    for (int tick = 0; tick < 3; ++tick) {
        camera.update(player, collision, kTick);
        CHECK(std::fabs(std::remainder(camera.yaw() - 3.0f, 2.0f * kPi)) < 0.4f);
    }
    for (int tick = 0; tick < 20; ++tick) {
        camera.update(player, collision, kTick);
    }
    CHECK(std::fabs(std::remainder(camera.yaw() - player.yaw, 2.0f * kPi)) < 1.0e-3f);
}

// One tick of a jump of Kain: z follows 565.685 t - 1250 t^2 until the landing.
bo2::game::PlayerState jump_state(float seconds) {
    bo2::game::PlayerState player;
    if (seconds < 0.0f) {
        player.grounded = true;
        return player;  // on the ground
    }
    const float height = 565.685f * seconds - 1250.0f * seconds * seconds;
    player.position.z = std::max(0.0f, height);
    player.grounded = height <= 0.0f && seconds > 0.1f;
    player.vertical_velocity = 565.685f - 2500.0f * seconds;
    return player;
}

void test_pivot_lags_a_jump() {
    // At takeoff the camera gets a setup with a 0.1 s delay and a 0.6 s blend
    // (FUN_004074c0): the pivot waits at its resting height, then blends to the
    // height above Kain, so it trails him in the air and catches up by the
    // landing. The oracle's eye z rose to 356.0 for a jump that took Kain to 281.
    const bo2::engine::LevelGeometry far_wall = make_wall(100000.0f);
    bo2::engine::CollisionWorld collision(far_wall);
    bo2::game::FollowCamera camera;
    bo2::game::PlayerState ground = jump_state(-1.0f);
    camera.update(ground, collision);
    CHECK(std::fabs(camera.pivot_z() - 76.0f) < 1.0e-4f);
    float apex_gap = 0.0f;
    for (int tick = 1; tick * kTick < 0.75; ++tick) {
        const float seconds = static_cast<float>(tick * kTick);
        const bo2::game::PlayerState player = jump_state(seconds);
        camera.update(player, collision, kTick);
        if (seconds < 0.09f) {
            CHECK(std::fabs(camera.pivot_z() - 76.0f) < 1.0e-4f);  // waiting
        }
        apex_gap = std::max(apex_gap, player.position.z + 76.0f - camera.pivot_z());
    }
    CHECK(apex_gap > 15.0f);
    // After the blend (0.6 s) and a landing the pivot is rigid again.
    const bo2::game::PlayerState landed = jump_state(-1.0f);
    for (int tick = 0; tick < 5; ++tick) {
        camera.update(landed, collision, kTick);
    }
    CHECK(std::fabs(camera.pivot_z() - 76.0f) < 1.0e-3f);
}

void test_a_fall_does_not_start_the_lag() {
    // Walking off an edge leaves the ground with a downward speed: no jump setup.
    const bo2::engine::LevelGeometry far_wall = make_wall(100000.0f);
    bo2::engine::CollisionWorld collision(far_wall);
    bo2::game::FollowCamera camera;
    bo2::game::PlayerState player;
    player.position.z = 100.0f;
    player.grounded = true;
    camera.update(player, collision);
    player.grounded = false;
    player.vertical_velocity = -50.0f;
    player.position.z = 99.0f;
    camera.update(player, collision, kTick);
    CHECK(std::fabs(camera.pivot_z() - (99.0f + 76.0f)) < 1.0e-4f);
}

}  // namespace

int main() {
    test_default_pose_matches_the_oracle();
    test_pose_follows_position_and_yaw();
    test_wall_pulls_the_camera_in();
    test_yaw_stays_close_on_a_tank_turn();
    test_yaw_catch_up_is_limited_by_the_acceleration();
    test_yaw_takes_the_short_way_round();
    test_pivot_lags_a_jump();
    test_a_fall_does_not_start_the_lag();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "camera tests: ok\n";
    return 0;
}
