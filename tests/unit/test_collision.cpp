#include <cmath>
#include <cstring>
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

void put_u16(std::vector<std::uint8_t>& data, std::uint16_t value) {
    data.push_back(static_cast<std::uint8_t>(value & 0xFF));
    data.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void put_f32(std::vector<std::uint8_t>& data, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    put_u32(data, bits);
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

void put_packed_triangle(std::vector<std::uint8_t>& data, std::uint8_t a, std::uint8_t b,
                         std::uint8_t c, std::uint8_t attribute) {
    data.push_back(a);
    data.push_back(static_cast<std::uint8_t>(b << 1));
    data.push_back(static_cast<std::uint8_t>(c << 2));
    data.push_back(attribute);
}

// A complete payload shaped like the reference model 0: a 160 x 64 x 80 box
// built from four rails plus the model AABB diagonal, twelve triangles and a
// single face anchored at the diagonal.
std::vector<std::uint8_t> make_box_model_payload() {
    std::vector<std::uint8_t> data = {'d', 'e', 'f', 'a', 'u', 'l', 't', 0};
    put_u32(data, 20);
    for (int index = 0; index < 20; ++index) {
        data.push_back(0);
    }
    put_u32(data, 0xFFFFFF01U);
    put_u32(data, 5 * 12);
    const std::int16_t records[5][6] = {{-80, -32, 0, 80, -32, 0},
                                        {-80, 32, 0, 80, 32, 0},
                                        {-80, -32, 80, 80, -32, 80},
                                        {-80, 32, 80, 80, 32, 80},
                                        {-80, -32, 0, 80, 32, 80}};
    for (const auto& record : records) {
        for (std::int16_t value : record) {
            put_i16(data, value);
        }
    }
    const std::uint8_t triangles[12][3] = {{3, 2, 0}, {0, 1, 3}, {7, 5, 4}, {4, 6, 7},
                                           {5, 1, 0}, {0, 4, 5}, {7, 3, 1}, {1, 5, 7},
                                           {6, 2, 3}, {3, 7, 6}, {4, 0, 2}, {2, 6, 4}};
    put_u32(data, 12);
    put_u32(data, 48);
    for (const auto& triangle : triangles) {
        put_packed_triangle(data, triangle[0], triangle[1], triangle[2], 0x18);
    }
    put_u32(data, 0);  // spheres
    put_u32(data, 1);  // faces
    put_u16(data, 8);
    put_u16(data, 9);
    put_u32(data, 0);
    put_u32(data, 0);
    put_u16(data, 1);
    put_u16(data, 12);
    put_u32(data, 0);
    for (int index = 0; index < 28; ++index) {
        data.push_back(0);
    }
    return data;
}

std::vector<std::uint8_t> make_sphere_payload() {
    std::vector<std::uint8_t> data = {'d', 'e', 'f', 'a', 'u', 'l', 't', 0};
    put_u32(data, 20);
    for (int index = 0; index < 20; ++index) {
        data.push_back(0);
    }
    put_u32(data, 0xFFFFFF01U);
    put_u32(data, 12);
    const std::int16_t values[6] = {-1, -1, -1, 1, 1, 1};
    for (const std::int16_t value : values) {
        put_i16(data, value);
    }
    put_u32(data, 0);  // triangles
    put_u32(data, 0);
    put_u32(data, 1);  // spheres
    put_f32(data, 0.0f);
    put_f32(data, 0.0f);
    put_f32(data, 5.0f);
    put_f32(data, 2.5f);
    put_u32(data, 3);
    put_u32(data, 0);
    put_u32(data, 0);  // faces
    return data;
}

void test_collmodc_model() {
    const bo2::engine::CollisionModel model =
        bo2::engine::parse_collmodc(make_box_model_payload());
    CHECK(model.valid);
    CHECK(model.vertices.size() == 10);
    CHECK(model.triangles.size() == 12);
    CHECK(model.faces.size() == 1);
    CHECK(model.spheres.empty());
    CHECK(model.vertices[1].x == 80.0f);
    CHECK(model.triangles[0].a == 3);
    CHECK(model.triangles[0].b == 2);
    CHECK(model.triangles[0].c == 0);
    CHECK(model.triangles[0].attribute == 0x18);
    CHECK(model.faces[0].has_triangles);
    CHECK(model.faces[0].triangle_count == 12);
    CHECK(model.faces[0].first_triangle == 0);
    CHECK(model.faces[0].bounds.minimum.x == -80.0f);
    CHECK(model.faces[0].bounds.maximum.z == 80.0f);
    CHECK(model.bounds.minimum.z == 0.0f);
    CHECK(model.bounds.maximum.z == 80.0f);

    const bo2::engine::CollisionModel spheres =
        bo2::engine::parse_collmodc(make_sphere_payload());
    CHECK(spheres.valid);
    CHECK(spheres.spheres.size() == 1);
    CHECK(spheres.spheres[0].center.z == 5.0f);
    CHECK(spheres.spheres[0].radius == 2.5f);
    CHECK(spheres.spheres[0].bone == 3);

    CHECK(!bo2::engine::parse_collmodc({1, 2, 3}).valid);
}

void test_collmodc_boxes() {
    const std::vector<bo2::engine::LevelCollisionBox> boxes =
        bo2::engine::parse_collmodc_boxes(make_collision_payload());
    CHECK(boxes.size() == 1);
    CHECK(boxes[0].minimum.x == -10.0f);
    CHECK(boxes[0].maximum.z == 10.0f);

    const std::vector<bo2::engine::LevelCollisionBox> model_boxes =
        bo2::engine::parse_collmodc_boxes(make_box_model_payload());
    CHECK(model_boxes.size() == 1);
    CHECK(model_boxes[0].minimum.x == -80.0f);
    CHECK(model_boxes[0].minimum.z == 0.0f);

    bo2::engine::LevelGeometry geometry;
    bo2::engine::LevelCollision collision;
    collision.model = 0;
    collision.data = make_box_model_payload();
    geometry.collisions.push_back(collision);
    bo2::engine::LevelInstance instance;
    instance.model = 0;
    instance.position = {100.0f, 0.0f, 0.0f};
    geometry.instances.push_back(instance);

    const bo2::engine::CollisionWorld hull(geometry,
                                            bo2::engine::CollisionSource::Collmodc);
    CHECK(hull.triangles() == 12);
    // The hull is a surface: a point inside the box is not a hit, a sphere
    // overlapping the top face is.
    CHECK(!hull.intersects_sphere({100.0f, 0.0f, 10.0f}, 1.0f));
    CHECK(hull.intersects_sphere({100.0f, 0.0f, 81.0f}, 2.0f));
    CHECK(!hull.intersects_sphere({100.0f, 0.0f, 100.0f}, 1.0f));
    const auto hull_hit = hull.raycast({100.0f, 0.0f, 100.0f}, {0.0f, 0.0f, -1.0f}, 200.0f);
    CHECK(hull_hit.has_value());
    CHECK(std::fabs(hull_hit->distance - 20.0f) < 1.0e-4f);
    CHECK(hull_hit->normal.z > 0.9f);

    const bo2::engine::CollisionWorld boxes_world(geometry,
                                                  bo2::engine::CollisionSource::Boxes);
    CHECK(boxes_world.boxes() == 1);
    CHECK(boxes_world.triangles() == 0);
    CHECK(boxes_world.intersects_sphere({100.0f, 0.0f, 0.0f}, 1.0f));
}

bo2::engine::LevelGeometry make_wall() {
    bo2::engine::LevelGeometry geometry;
    geometry.positions = {{0.0f, -100.0f, 0.0f}, {0.0f, 100.0f, 0.0f}, {0.0f, 100.0f, 100.0f},
                          {0.0f, -100.0f, 100.0f}};
    for (std::uint32_t base = 0; base < 2; ++base) {
        bo2::engine::LevelTriangle triangle;
        triangle.a = base;
        triangle.b = base + 1;
        triangle.c = base + 2;
        geometry.triangles.push_back(triangle);
    }
    return geometry;
}

// A wall that only exists between z = 30 and z = 40.
bo2::engine::LevelGeometry make_low_wall() {
    bo2::engine::LevelGeometry geometry;
    geometry.positions = {{0.0f, -100.0f, 30.0f}, {0.0f, 100.0f, 30.0f}, {0.0f, 100.0f, 40.0f},
                          {0.0f, -100.0f, 40.0f}};
    for (std::uint32_t base = 0; base < 2; ++base) {
        bo2::engine::LevelTriangle triangle;
        triangle.a = base;
        triangle.b = base + 1;
        triangle.c = base + 2;
        geometry.triangles.push_back(triangle);
    }
    return geometry;
}

void test_sphere_contact() {
    const bo2::engine::CollisionWorld world(make_wall());
    const auto contact = world.sphere_contact({0.3f, 0.0f, 50.0f}, 0.5f, -1);
    CHECK(contact.has_value());
    CHECK(std::fabs(contact->distance - 0.3f) < 1.0e-4f);
    CHECK(contact->normal.x > 0.99f);
    CHECK(std::fabs(contact->normal.y) < 1.0e-3f);
    CHECK(!world.sphere_contact({5.0f, 0.0f, 50.0f}, 0.5f, -1).has_value());
}

void test_capsule_contact() {
    const bo2::engine::CollisionWorld wall(make_wall());
    const auto full = wall.capsule_contact({0.3f, 0.0f, 0.4f}, 71.2f, 0.4f, -1);
    CHECK(full.has_value());
    CHECK(std::fabs(full->distance - 0.3f) < 1.0e-4f);
    CHECK(full->normal.x > 0.99f);

    // The segment spans the low wall, so the capsule sees it even though its
    // ends (z = 0.4 and 71.6) are far from the band.
    const bo2::engine::CollisionWorld low(make_low_wall());
    CHECK(low.capsule_contact({0.3f, 0.0f, 0.4f}, 71.2f, 0.4f, -1).has_value());
    CHECK(!low.capsule_contact({5.0f, 0.0f, 0.4f}, 71.2f, 0.4f, -1).has_value());
}

// The run probe and the ground probe are swept spheres (FUN_005be9a0), not a
// row of samples: the first contact along the offset wins.
void test_swept_sphere() {
    const bo2::engine::CollisionWorld wall(make_wall());
    const auto hit = wall.swept_sphere({5.0f, 0.0f, 50.0f}, {-10.0f, 0.0f, 0.0f}, 0.5f, -1);
    CHECK(hit.has_value());
    CHECK(std::fabs(hit->distance - 4.5f) < 1.0e-3f);
    CHECK(hit->normal.x > 0.99f);
    CHECK(!wall.swept_sphere({5.0f, 0.0f, 50.0f}, {0.0f, 10.0f, 0.0f}, 0.5f, -1).has_value());
    CHECK(!wall.swept_sphere({5.0f, 0.0f, 50.0f}, {10.0f, 0.0f, 0.0f}, 0.5f, -1).has_value());
    CHECK(!wall.swept_sphere({5.0f, 0.0f, 50.0f}, {-4.0f, 0.0f, 0.0f}, 0.5f, -1).has_value());
    const auto touching =
        wall.swept_sphere({5.0f, 0.0f, 50.0f}, {-4.5f, 0.0f, 0.0f}, 0.5f, -1);
    CHECK(touching.has_value());
    CHECK(std::fabs(touching->distance - 4.5f) < 1.0e-2f);
}

// The body is the model AABB (-20,-20,0)..(20,20,72): the box contact gives
// the minimum-translation axis and the penetration depth.
void test_box_contact() {
    const bo2::engine::CollisionWorld wall(make_wall());
    const auto contact = wall.box_contact({-0.5f, 0.0f, 50.0f}, {1.0f, 1.0f, 1.0f}, -1);
    CHECK(contact.has_value());
    CHECK(contact->normal.x < -0.99f);
    CHECK(std::fabs(contact->normal.y) < 1.0e-3f);
    CHECK(std::fabs(contact->distance - 0.5f) < 1.0e-3f);
    // Touching faces do not overlap.
    CHECK(!wall.box_contact({-1.0f, 0.0f, 50.0f}, {1.0f, 1.0f, 1.0f}, -1).has_value());
    CHECK(!wall.box_contact({5.0f, 0.0f, 50.0f}, {1.0f, 1.0f, 1.0f}, -1).has_value());

    // A box that spans the low wall's band reports the wall, not the gap.
    const bo2::engine::CollisionWorld low(make_low_wall());
    const auto band = low.box_contact({-0.5f, 0.0f, 35.0f}, {1.0f, 1.0f, 20.0f}, -1);
    CHECK(band.has_value());
    CHECK(band->normal.x < -0.99f);
}

// Two walls, one in a neighbouring cell and one in a cell that only shares a
// portal *symbol*, not an index. The portal fields are symbols (BO2NX-76).
void test_portal_symbols_link_cells() {
    bo2::engine::LevelGeometry geometry;
    geometry.positions = {{10.0f, -10.0f, 0.0f}, {10.0f, 10.0f, 0.0f}, {10.0f, 10.0f, 10.0f},
                          {10.0f, -10.0f, 10.0f}, {20.0f, -10.0f, 0.0f}, {20.0f, 10.0f, 0.0f},
                          {20.0f, 10.0f, 10.0f}, {20.0f, -10.0f, 10.0f}};
    for (std::uint32_t base = 0; base < 2; ++base) {
        bo2::engine::LevelTriangle near;
        near.a = base;
        near.b = base + 1;
        near.c = base + 2;
        near.instance = 0;
        geometry.triangles.push_back(near);
        bo2::engine::LevelTriangle far;
        far.a = base + 4;
        far.b = base + 5;
        far.c = base + 6;
        far.instance = 1;
        geometry.triangles.push_back(far);
    }
    geometry.instances.resize(2);
    geometry.instance_cells = {1, 2};
    geometry.cell_grid.cells.resize(3);
    bo2::engine::LevelCell& first = geometry.cell_grid.cells[0];
    first.symbol = 10;
    first.minimum = {-100.0f, -100.0f, -100.0f};
    first.maximum = {9.9f, 100.0f, 100.0f};
    first.portals.push_back(bo2::engine::CellPortal{});
    first.portals.back().version = 1;
    first.portals.back().cell_a = 99;  // the portal's own symbol
    first.portals.back().cell_b = 20;  // the neighbouring cell's symbol
    bo2::engine::LevelCell& second = geometry.cell_grid.cells[1];
    second.symbol = 20;
    second.minimum = {9.9f, -100.0f, -100.0f};
    second.maximum = {15.0f, 100.0f, 100.0f};
    bo2::engine::LevelCell& third = geometry.cell_grid.cells[2];
    third.symbol = 30;
    third.minimum = {15.0f, -100.0f, -100.0f};
    third.maximum = {30.0f, 100.0f, 100.0f};

    const bo2::engine::CollisionWorld world(geometry);
    // The wall in cell 1 is reachable through cell 0's portal.
    CHECK(world.intersects_sphere({9.0f, 0.0f, 5.0f}, 2.0f, 0));
    // The wall in cell 2 is not a neighbour and stays out of the query.
    CHECK(!world.intersects_sphere({19.0f, 0.0f, 5.0f}, 2.0f, 0));
    // The owner cell still sees its own wall.
    CHECK(world.intersects_sphere({19.0f, 0.0f, 5.0f}, 2.0f, 2));
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
    test_portal_symbols_link_cells();
    test_sphere_contact();
    test_capsule_contact();
    test_swept_sphere();
    test_box_contact();
    test_collmodc_model();
    test_collmodc_boxes();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "collision tests: ok\n";
    return 0;
}
