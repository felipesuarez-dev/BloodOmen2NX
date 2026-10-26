#include <cmath>
#include <iostream>
#include <vector>

#include "bo2/engine/collision.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

bo2::engine::LevelGeometry make_plane() {
    bo2::engine::LevelGeometry geometry;
    geometry.positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    bo2::engine::LevelTriangle triangle;
    triangle.a = 0;
    triangle.b = 1;
    triangle.c = 2;
    geometry.triangles.push_back(triangle);
    return geometry;
}

void test_raycast() {
    const bo2::engine::CollisionWorld world(make_plane());
    CHECK(world.triangles() == 1);
    const auto hit = world.raycast({0.2f, 0.2f, 1.0f}, {0.0f, 0.0f, -1.0f}, 10.0f);
    CHECK(hit.has_value());
    CHECK(std::fabs(hit->distance - 1.0f) < 1.0e-5f);
    CHECK(!world.raycast({2.0f, 2.0f, 1.0f}, {0.0f, 0.0f, -1.0f}, 10.0f).has_value());
    CHECK(!world.raycast({0.2f, 0.2f, 1.0f}, {0.0f, 0.0f, 1.0f}, 10.0f).has_value());
}

void test_sphere() {
    const bo2::engine::CollisionWorld world(make_plane());
    CHECK(world.intersects_sphere({0.2f, 0.2f, 0.1f}, 0.2f));
    CHECK(!world.intersects_sphere({0.2f, 0.2f, 1.0f}, 0.2f));
    CHECK(world.intersects_sphere({0.0f, 0.0f, 0.0f}, 0.0f));
}

}  // namespace

int main() {
    test_raycast();
    test_sphere();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "collision tests: ok\n";
    return 0;
}
