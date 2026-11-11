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

void put_u32(std::vector<std::uint8_t>& data, std::uint32_t value) {
    data.push_back(static_cast<std::uint8_t>(value & 0xFF));
    data.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    data.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    data.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void put_i16(std::vector<std::uint8_t>& data, std::int16_t value) {
    const auto bits = static_cast<std::uint16_t>(value);
    data.push_back(static_cast<std::uint8_t>(bits & 0xFF));
    data.push_back(static_cast<std::uint8_t>((bits >> 8) & 0xFF));
}

std::vector<std::uint8_t> make_collision_payload() {
    std::vector<std::uint8_t> data = {'d', 'e', 'f', 'a', 'u', 'l', 't', 0};
    put_u32(data, 20);
    for (int index = 0; index < 20; ++index) {
        data.push_back(0);
    }
    put_u32(data, 0xFFFFFF01U);
    put_u32(data, 12);
    const std::int16_t values[6] = {static_cast<std::int16_t>(-10),
                                    static_cast<std::int16_t>(-10),
                                    static_cast<std::int16_t>(-10),
                                    static_cast<std::int16_t>(10),
                                    static_cast<std::int16_t>(10),
                                    static_cast<std::int16_t>(10)};
    for (const std::int16_t value : values) {
        put_i16(data, value);
    }
    return data;
}

void test_collmodc_boxes() {
    const std::vector<bo2::engine::LevelCollisionBox> boxes =
        bo2::engine::parse_collmodc_boxes(make_collision_payload());
    CHECK(boxes.size() == 1);
    CHECK(boxes[0].minimum.x == -10.0f);
    CHECK(boxes[0].maximum.z == 10.0f);

    bo2::engine::LevelGeometry geometry;
    bo2::engine::LevelCollision collision;
    collision.model = 0;
    collision.data = make_collision_payload();
    geometry.collisions.push_back(collision);
    bo2::engine::LevelInstance instance;
    instance.model = 0;
    instance.position = {100.0f, 0.0f, 0.0f};
    geometry.instances.push_back(instance);
    const bo2::engine::CollisionWorld world(geometry, true);
    CHECK(world.boxes() == 1);
    CHECK(world.intersects_sphere({100.0f, 0.0f, 0.0f}, 1.0f));
    CHECK(!world.intersects_sphere({100.0f, 0.0f, 100.0f}, 1.0f));
    const auto hit = world.raycast({100.0f, 0.0f, 100.0f}, {0.0f, 0.0f, -1.0f}, 200.0f);
    CHECK(hit.has_value());
    CHECK(std::fabs(hit->distance - 90.0f) < 1.0e-4f);
    CHECK(hit->normal.z > 0.9f);
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
    test_collmodc_boxes();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "collision tests: ok\n";
    return 0;
}
