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

// A wall across the x axis at x = wall_x, tall enough to catch the camera pivot.
bo2::engine::LevelGeometry make_wall(float wall_x) {
    bo2::engine::LevelGeometry geometry;
    geometry.positions = {{wall_x, -500.0f, 0.0f}, {wall_x, 500.0f, 0.0f},
                          {wall_x, 500.0f, 300.0f}, {wall_x, -500.0f, 300.0f}};
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
    // 190.9 behind and 96.1 above Kain along his yaw.
    CHECK(std::fabs(camera.eye().x + 190.9f) < 1.0e-3f);
    CHECK(std::fabs(camera.eye().y) < 1.0e-3f);
    CHECK(std::fabs(camera.eye().z - 96.1f) < 1.0e-3f);
    // Looking along the yaw, pitched down 6 degrees.
    const bo2::engine::Vec3 view = view_direction(camera);
    CHECK(std::fabs(view.z - std::sin(-6.0f * 3.14159265f / 180.0f)) < 1.0e-4f);
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
    CHECK(std::fabs(camera.eye().y - (50.0f - 190.9f)) < 1.0e-2f);
    CHECK(std::fabs(camera.eye().z - (20.0f + 96.1f)) < 1.0e-3f);
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

}  // namespace

int main() {
    test_default_pose_matches_the_oracle();
    test_pose_follows_position_and_yaw();
    test_wall_pulls_the_camera_in();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "camera tests: ok\n";
    return 0;
}
