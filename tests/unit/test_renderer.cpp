#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "bo2/engine/cell_group.hpp"
#include "bo2/engine/image.hpp"
#include "bo2/platform/headless/software_renderer.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

template <typename Function>
bool throws(Function&& function) {
    try {
        function();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

bo2::engine::LevelGeometry level_quad() {
    bo2::engine::LevelGeometry geometry;
    geometry.positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    geometry.normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
    geometry.triangles = {{0, 1, 2, 0, 0}, {0, 2, 3, 0, 0}};
    geometry.minimum = {0.0f, 0.0f, 0.0f};
    geometry.maximum = {1.0f, 1.0f, 0.0f};
    return geometry;
}

bo2::engine::Image red_image() {
    bo2::engine::Image image;
    image.width = 4;
    image.height = 4;
    image.rgba.assign(4 * 4 * 4, 0);
    for (std::size_t pixel = 0; pixel < 16; ++pixel) {
        image.rgba[pixel * 4 + 0] = 255;
        image.rgba[pixel * 4 + 3] = 255;
    }
    return image;
}

}  // namespace

int main() {
    const bo2::engine::Image image = red_image();
    bo2::platform::headless::FrameOptions options;
    options.width = 64;
    options.height = 64;
    options.camera.eye = {0.0f, 0.0f, 3.0f};
    options.camera.target = {0.0f, 0.0f, 0.0f};
    options.camera.fov_degrees = 55.0f;

    const std::vector<std::uint8_t> frame =
        bo2::platform::headless::render_textured_cube(image, options);
    CHECK(frame.size() == 64U * 64U * 4U);
    const std::size_t corner = 0;
    const std::size_t center = (32U * 64U + 32U) * 4U;
    CHECK(frame[corner + 0] == 26 && frame[corner + 1] == 28 && frame[corner + 2] == 36);
    CHECK(frame[center + 0] > 150 && frame[center + 1] < 100);

    CHECK(throws([&] {
        bo2::engine::Image empty;
        bo2::platform::headless::render_textured_cube(empty, options);
    }));
    CHECK(throws([&] {
        options.camera.target = options.camera.eye;
        bo2::platform::headless::render_textured_cube(image, options);
    }));

    bo2::platform::headless::FrameOptions level_options;
    level_options.width = 64;
    level_options.height = 64;
    level_options.camera.eye = {0.5f, -3.0f, 3.0f};
    level_options.camera.target = {0.5f, 0.5f, 0.0f};
    level_options.camera.up = {0.0f, 0.0f, 1.0f};
    level_options.camera.fov_degrees = 55.0f;
    const std::vector<std::uint8_t> level_frame =
        bo2::platform::headless::render_level(level_quad(), level_options);
    CHECK(level_frame.size() == 64U * 64U * 4U);
    CHECK(level_frame[center + 0] != 20 || level_frame[center + 1] != 22 ||
          level_frame[center + 2] != 30);
    CHECK(throws([&] {
        bo2::engine::LevelGeometry empty;
        bo2::platform::headless::render_level(empty, level_options);
    }));

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "renderer tests: ok\n";
    return 0;
}
