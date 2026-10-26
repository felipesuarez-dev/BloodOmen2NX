#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "bo2/engine/math.hpp"
#include "bo2/engine/world.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

bo2::engine::Frustum camera_frustum() {
    const bo2::engine::Mat4 view = bo2::engine::Mat4::look_at(
        {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
    const bo2::engine::Mat4 projection =
        bo2::engine::Mat4::perspective(1.0f, 1.0f, 0.1f, 100.0f);
    return bo2::engine::frustum_from_view_projection(projection * view);
}

void test_frustum() {
    const bo2::engine::Frustum frustum = camera_frustum();
    CHECK(frustum.intersects_sphere({0.0f, 0.0f, -5.0f}, 0.5f));
    CHECK(!frustum.intersects_sphere({0.0f, 0.0f, 5.0f}, 0.5f));
    CHECK(!frustum.intersects_sphere({50.0f, 0.0f, -5.0f}, 0.5f));
    CHECK(frustum.intersects_sphere({1.5f, 0.0f, -5.0f}, 2.0f));
}

void test_entity_world() {
    bo2::engine::EntityWorld world;
    const auto a = world.add({0.0f, 0.0f, -5.0f}, 0.5f, 1);
    const auto b = world.add({0.0f, 0.0f, 5.0f}, 0.5f, 2);
    CHECK(world.entities().size() == 2);
    const bo2::engine::Frustum frustum = camera_frustum();
    const auto visible = world.visible(frustum);
    CHECK(visible.size() == 1);
    CHECK(visible[0] == a);
    CHECK(world.in_cell(1).size() == 1);
    CHECK(world.in_cell(2)[0] == b);
    CHECK(world.set_position(a, {0.0f, 0.0f, 5.0f}));
    CHECK(world.visible(frustum).empty());
    CHECK(world.remove(b));
    CHECK(world.entities().size() == 1);
    CHECK(!world.remove(999));
}

void test_cell_streamer() {
    std::vector<bo2::engine::CellBounds> cells = {
        {1, {-5.0f, -5.0f, -5.0f}, {5.0f, 5.0f, 5.0f}},
        {2, {100.0f, 100.0f, 100.0f}, {110.0f, 110.0f, 110.0f}},
    };
    bo2::engine::CellStreamer streamer(cells);
    auto active = streamer.update({0.0f, 0.0f, 0.0f}, 10.0f);
    CHECK(active.size() == 1);
    CHECK(streamer.is_active(1));
    CHECK(!streamer.is_active(2));
    active = streamer.update({105.0f, 105.0f, 105.0f}, 10.0f);
    CHECK(active.size() == 1);
    CHECK(streamer.is_active(2));
    CHECK(!streamer.is_active(1));
}

void test_invalid() {
    bool threw = false;
    try {
        bo2::engine::CellStreamer streamer(
            {{1, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 1.0f}}});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

}  // namespace

int main() {
    test_frustum();
    test_entity_world();
    test_cell_streamer();
    test_invalid();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "world tests: ok\n";
    return 0;
}
