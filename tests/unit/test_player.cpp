#include <cmath>
#include <iostream>

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

bo2::engine::LevelGeometry make_floor() {
    bo2::engine::LevelGeometry geometry;
    geometry.positions = {{-20.0f, -20.0f, 0.0f}, {20.0f, -20.0f, 0.0f}, {20.0f, 20.0f, 0.0f},
                          {-20.0f, 20.0f, 0.0f}};
    geometry.triangles = {{0, 1, 2, 0, 0, 0xFFFFFFFFU, -1, -1},
                          {0, 2, 3, 0, 0, 0xFFFFFFFFU, -1, -1}};
    return geometry;
}

void test_movement_and_collision() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f});
    bo2::game::PlayerInput input;
    input.move_y = 1.0f;
    player.step(input, 1.0);
    CHECK(player.state().position.x > 1.0f);
    CHECK(std::fabs(player.state().position.z) < 1.0e-5f);
    CHECK(player.state().grounded);
    const float walk_x = player.state().position.x;
    input.run = true;
    player.step(input, 1.0);
    CHECK(player.state().position.x > walk_x + 3.0f);
}

void test_jump_and_land() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f});
    bo2::game::PlayerInput input;
    player.step(input, 0.016);
    CHECK(player.state().grounded);
    input.jump = true;
    player.step(input, 0.016);
    CHECK(!player.state().grounded);
    CHECK(player.state().vertical_velocity > 0.0f);
    input.jump = false;
    for (int tick = 0; tick < 200; ++tick) {
        player.step(input, 0.016);
    }
    CHECK(player.state().grounded);
    CHECK(std::fabs(player.state().position.z) < 1.0e-4f);
}

void test_attack_and_damage() {
    const bo2::engine::LevelGeometry floor = make_floor();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f});
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
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f});
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
    bo2::game::PlayerController player(collision, {0.0f, 0.0f, 0.0f});
    bo2::game::PlayerInput input;
    input.look_x = 1.0f;
    player.step(input, 0.1);
    CHECK(player.state().yaw < 0.0f);
    CHECK(std::fabs(player.camera_eye().z - 1.7f) < 1.0e-5f);
}

}  // namespace

int main() {
    test_movement_and_collision();
    test_jump_and_land();
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
