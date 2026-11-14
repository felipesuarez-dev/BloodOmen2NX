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

// A triangle record as the game stores it (FUN_0062dfc0): three 9-bit indices,
// a at bits 18..26, b at bits 9..17 and c at bits 0..8. The top byte keeps the
// five bits of `attribute` above the three it shares with `a`.
void put_packed_triangle(std::vector<std::uint8_t>& data, std::uint32_t a, std::uint32_t b,
                         std::uint32_t c, std::uint8_t attribute) {
    put_u32(data, (static_cast<std::uint32_t>(attribute & 0xF8U) << 24) | ((a & 0x1FFU) << 18) |
                      ((b & 0x1FFU) << 9) | (c & 0x1FFU));
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

// The pair test unpacks three 9-bit indices from a record, and two dwords with
// 16-bit indices when bit 31 is set (FUN_0062dfc0). Every field can pass 127
// and 255, and a record after a wide one is still read at its place.
void test_collmodc_triangle_records() {
    std::vector<std::uint8_t> data = {'d', 'e', 'f', 'a', 'u', 'l', 't', 0};
    put_u32(data, 20);
    for (int index = 0; index < 20; ++index) {
        data.push_back(0);
    }
    put_u32(data, 0xFFFFFF01U);
    put_u32(data, 420 * 6);
    for (std::int16_t index = 0; index < 420; ++index) {
        put_i16(data, index);
        put_i16(data, 0);
        put_i16(data, 0);
    }
    put_u32(data, 1);
    put_u32(data, 16);
    put_packed_triangle(data, 300, 200, 400, 0x18);
    put_u32(data, 0x80000000U | (1U << 29) | 291U);  // wide, category 1, a = 291
    put_u16(data, 7);
    put_u16(data, 419);
    put_packed_triangle(data, 5, 6, 7, 0x40);  // category 2
    put_u32(data, 0);
    // One face over the last record: its slice starts at byte 12 of the list.
    put_u32(data, 1);
    put_u16(data, 0);
    put_u16(data, 1);
    put_u32(data, 0);
    put_u32(data, 0);
    put_u16(data, 0);
    put_u16(data, 1);
    put_u32(data, 12);
    const bo2::engine::CollisionModel model = bo2::engine::parse_collmodc(data);
    CHECK(model.valid);
    CHECK(model.vertices.size() == 420);
    CHECK(model.triangles.size() == 3);
    if (model.triangles.size() != 3) {
        return;
    }
    CHECK(model.triangles[0].a == 300 && model.triangles[0].b == 200 &&
          model.triangles[0].c == 400);
    CHECK(model.triangles[0].category == 0);
    CHECK(model.triangles[1].a == 291 && model.triangles[1].b == 7 &&
          model.triangles[1].c == 419);
    CHECK(model.triangles[1].category == 1);
    CHECK(model.triangles[2].a == 5 && model.triangles[2].b == 6 && model.triangles[2].c == 7);
    CHECK(model.triangles[2].category == 2);
    // The face slice counts records, not dwords: byte 12 is the third record.
    CHECK(model.faces.size() == 1);
    if (model.faces.size() == 1) {
        CHECK(model.faces[0].first_triangle == 2 && model.faces[0].triangle_count == 1);
    }

    // A wide record cut after its first dword is not a model.
    std::vector<std::uint8_t> cut = {'d', 'e', 'f', 'a', 'u', 'l', 't', 0};
    put_u32(cut, 20);
    for (int index = 0; index < 20; ++index) {
        cut.push_back(0);
    }
    put_u32(cut, 0xFFFFFF01U);
    put_u32(cut, 6);
    put_i16(cut, 0);
    put_i16(cut, 0);
    put_i16(cut, 0);
    put_u32(cut, 1);
    put_u32(cut, 4);
    put_u32(cut, 0x80000000U);
    put_u32(cut, 0);
    put_u32(cut, 0);
    CHECK(!bo2::engine::parse_collmodc(cut).valid);
}

// The swept body (FUN_005ccb60): a sphere stops at a head-on wall and slides
// along a 45 degree one.
void test_slide_body() {
    const bo2::engine::CollisionWorld wall(make_wall());
    const std::vector<bo2::engine::BodySphere> body = {{{0.0f, 0.0f, 51.98f}, 19.8f}};
    const bo2::engine::BodyMove stopped =
        wall.slide_body({50.0f, 0.0f, 0.0f}, body, {-60.0f, 0.0f, 0.0f}, -1);
    CHECK(stopped.blocked);
    CHECK(stopped.normal.x > 0.99f);
    CHECK(std::fabs(stopped.position.x - 19.8f) < 0.5f);

    bo2::engine::LevelGeometry level;
    level.positions = {{50.0f, 0.0f, 0.0f}, {0.0f, 50.0f, 0.0f}, {0.0f, 50.0f, 300.0f},
                       {50.0f, 0.0f, 300.0f}};
    const std::uint32_t triangles[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (const auto& triangle : triangles) {
        bo2::engine::LevelTriangle value;
        value.a = triangle[0];
        value.b = triangle[1];
        value.c = triangle[2];
        level.triangles.push_back(value);
    }
    const bo2::engine::CollisionWorld slanted(level);
    const bo2::engine::BodyMove slid =
        slanted.slide_body({0.0f, 0.0f, 0.0f}, body, {40.0f, 0.0f, 0.0f}, -1);
    CHECK(slid.blocked);
    const float distance =
        (slid.position.x + slid.position.y - 50.0f) / std::sqrt(2.0f);
    CHECK(std::fabs(distance + 19.8f) < 0.5f);
    CHECK(slid.position.x > 15.0f);
    CHECK(slid.position.y < -5.0f);
}

// The triangle test of a moving sphere (FUN_005ddc90). The face stops a mover
// only from its front, that is, a mover that goes against the normal. The
// edges and the vertices follow the category of the record: 0 only from the
// front, 1 from both sides, 2 never.
void test_narrow_phase_sides_and_categories() {
    const auto make_payload = [](std::uint8_t attribute) {
        std::vector<std::uint8_t> data = {'d', 'e', 'f', 'a', 'u', 'l', 't', 0};
        put_u32(data, 20);
        for (int index = 0; index < 20; ++index) {
            data.push_back(0);
        }
        put_u32(data, 0xFFFFFF01U);
        put_u32(data, 18);
        const std::int16_t points[3][3] = {{-10, -10, 0}, {10, -10, 0}, {0, 10, 0}};
        for (const auto& point : points) {
            put_i16(data, point[0]);
            put_i16(data, point[1]);
            put_i16(data, point[2]);
        }
        data.push_back(0);
        data.push_back(0);
        put_u32(data, 1);
        put_u32(data, 4);
        put_packed_triangle(data, 0, 1, 2, attribute);
        put_u32(data, 0);
        put_u32(data, 0);
        return data;
    };
    const auto make_world = [&make_payload](std::uint8_t attribute) {
        bo2::engine::LevelGeometry geometry;
        bo2::engine::LevelCollision collision;
        collision.model = 0;
        collision.data = make_payload(attribute);
        geometry.collisions.push_back(std::move(collision));
        bo2::engine::LevelInstance instance;
        instance.model = 0;
        geometry.instances.push_back(instance);
        return bo2::engine::CollisionWorld(geometry,
                                           bo2::engine::CollisionSource::Collmodc);
    };
    // The triangle lies at z = 0 with its normal up. A sphere of radius 0.5
    // comes down or up over its middle (the face), or 0.3 outside the edge at
    // y = -10 (the edge alone).
    const auto hits = [](const bo2::engine::CollisionWorld& world, float y, bool from_above) {
        const bo2::engine::Vec3 origin{0.0f, y, from_above ? 5.0f : -5.0f};
        return world
            .swept_sphere(origin, from_above ? bo2::engine::Vec3{0.0f, 0.0f, -10.0f}
                                             : bo2::engine::Vec3{0.0f, 0.0f, 10.0f},
                          0.5f, -1)
            .has_value();
    };
    const float face = 0.0f;
    const float edge = -10.3f;

    const auto plain = make_world(0x18);  // category 0
    CHECK(plain.triangles() == 1);
    CHECK(hits(plain, face, true));
    CHECK(!hits(plain, face, false));
    CHECK(hits(plain, edge, true));
    CHECK(!hits(plain, edge, false));

    const auto two_sided_edges = make_world(0x38);  // category 1
    CHECK(hits(two_sided_edges, face, true));
    CHECK(!hits(two_sided_edges, face, false));
    CHECK(hits(two_sided_edges, edge, true));
    CHECK(hits(two_sided_edges, edge, false));

    const auto face_only = make_world(0x58);  // category 2
    CHECK(hits(face_only, face, true));
    CHECK(!hits(face_only, face, false));
    CHECK(!hits(face_only, edge, true));
    CHECK(!hits(face_only, edge, false));

    // The separation of the body follows the same rule when it is given the
    // way the body moves: a triangle the sweep cannot touch does not push.
    const bo2::engine::Vec3 inside{0.0f, 0.0f, -0.2f};
    const bo2::engine::Vec3 going_up{0.0f, 0.0f, 1.0f};
    const bo2::engine::Vec3 going_down{0.0f, 0.0f, -1.0f};
    CHECK(plain.sphere_contact(inside, 0.5f, -1).has_value());
    CHECK(!plain.sphere_contact(inside, 0.5f, -1, &going_up).has_value());
    CHECK(plain.sphere_contact(inside, 0.5f, -1, &going_down).has_value());
    // The queries without a direction keep skipping a category 2 triangle.
    CHECK(!face_only.sphere_contact(inside, 0.5f, -1).has_value());
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

void test_render_source_skips_the_actor() {
    // A level floor at z = 0 and an actor's triangle above it at z = 5.
    bo2::engine::LevelGeometry geometry = make_plane();
    geometry.positions.push_back({0.0f, 0.0f, 5.0f});
    geometry.positions.push_back({1.0f, 0.0f, 5.0f});
    geometry.positions.push_back({0.0f, 1.0f, 5.0f});
    bo2::engine::LevelTriangle triangle;
    triangle.a = 3;
    triangle.b = 4;
    triangle.c = 5;
    geometry.triangles.push_back(triangle);
    const bo2::engine::Vec3 origin{0.2f, 0.2f, 9.0f};
    const bo2::engine::Vec3 down{0.0f, 0.0f, -1.0f};

    const bo2::engine::CollisionWorld solid(geometry);
    CHECK(solid.triangles() == 2);
    const auto blocked = solid.raycast(origin, down, 20.0f);
    CHECK(blocked.has_value() && std::fabs(blocked->distance - 4.0f) < 1.0e-4f);

    geometry.actor.first_vertex = 3;
    geometry.actor.vertex_count = 3;
    geometry.actor.first_triangle = 1;
    geometry.actor.triangle_count = 1;
    const bo2::engine::CollisionWorld world(geometry);
    CHECK(world.triangles() == 1);
    const auto floor = world.raycast(origin, down, 20.0f);
    CHECK(floor.has_value() && std::fabs(floor->distance - 9.0f) < 1.0e-4f);
    CHECK(!world.intersects_sphere({0.2f, 0.2f, 5.0f}, 0.5f));
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
    test_render_source_skips_the_actor();
    test_sphere();
    test_portal_symbols_link_cells();
    test_sphere_contact();
    test_capsule_contact();
    test_swept_sphere();
    test_box_contact();
    test_collmodc_model();
    test_collmodc_triangle_records();
    test_collmodc_boxes();
    test_slide_body();
    test_narrow_phase_sides_and_categories();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "collision tests: ok\n";
    return 0;
}
