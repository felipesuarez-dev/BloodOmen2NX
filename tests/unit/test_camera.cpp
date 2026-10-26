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

bo2::engine::LevelGeometry make_wall() {
    bo2::engine::LevelGeometry geometry;
    geometry.positions = {{2.0f, -5.0f, 0.0f}, {2.0f, 5.0f, 0.0f}, {2.0f, 5.0f, 5.0f},
                          {2.0f, -5.0f, 5.0f}};
    geometry.triangles = {{0, 1, 2, 0, 0, 0xFFFFFFFFU, -1, -1},
                          {0, 2, 3, 0, 0, 0xFFFFFFFFU, -1, -1}};
    return geometry;
}

void test_open_camera() {
    const bo2::engine::LevelGeometry floor = make_wall();
    bo2::engine::CollisionWorld collision(floor);
    bo2::game::FollowCamera camera(4.0f);
    bo2::game::PlayerState player;
    camera.update(player, collision);
    CHECK(std::fabs(camera.target().z - 1.7f) < 1.0e-5f);
    CHECK(std::fabs(camera.eye().x + 4.0f) < 1.0e-5f);
}

void test_wall_clamps_camera() {
    const bo2::engine::LevelGeometry wall = make_wall();
    bo2::engine::CollisionWorld collision(wall);
    bo2::game::FollowCamera camera(8.0f);
    bo2::game::PlayerState player;
    player.position = {1.0f, 0.0f, 0.0f};
    camera.update(player, collision);
    CHECK(camera.eye().x < 2.0f);
}

}  // namespace

int main() {
    test_open_camera();
    test_wall_clamps_camera();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "camera tests: ok\n";
    return 0;
}
