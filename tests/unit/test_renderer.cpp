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

    bo2::engine::LevelGeometry textured = level_quad();
    textured.texcoords = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    textured.texture_names = {"synthetic"};
    for (bo2::engine::LevelTriangle& triangle : textured.triangles) {
        triangle.texture = 0;
    }
    bo2::engine::Image texture;
    texture.width = 2;
    texture.height = 2;
    texture.rgba = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    const std::vector<bo2::engine::Image> textures = {texture};
    const std::vector<std::uint8_t> textured_frame =
        bo2::platform::headless::render_level(textured, textures, level_options);
    CHECK(textured_frame.size() == 64U * 64U * 4U);
    CHECK(textured_frame[center + 0] != 20 || textured_frame[center + 1] != 22 ||
          textured_frame[center + 2] != 30);

    bo2::engine::LevelGeometry colored = level_quad();
    colored.materials.resize(1);
    colored.materials[0].has_colors = true;
    colored.materials[0].colors[1] = {0.0f, 1.0f, 0.0f};
    colored.materials[0].has_controls = true;
    colored.materials[0].controls[1].weight = 1.0f;
    const std::vector<std::uint8_t> colored_frame =
        bo2::platform::headless::render_level(colored, level_options);
    CHECK(colored_frame[center + 1] > 150 && colored_frame[center + 0] < 100);

    bo2::engine::LevelGeometry tinted = level_quad();
    tinted.texcoords = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    for (bo2::engine::LevelTriangle& triangle : tinted.triangles) {
        triangle.texture = 0;
    }
    tinted.materials.resize(1);
    tinted.materials[0].has_colors = true;
    tinted.materials[0].colors[1] = {0.5f, 0.5f, 0.5f};
    bo2::engine::Image white;
    white.width = 1;
    white.height = 1;
    white.rgba = {255, 255, 255, 255};
    const std::vector<bo2::engine::Image> white_texture = {white};
    const std::vector<std::uint8_t> plain_frame =
        bo2::platform::headless::render_level(tinted, white_texture, level_options);
    bo2::platform::headless::FrameOptions tint_options = level_options;
    tint_options.material_mode = bo2::engine::MaterialMode::Diffuse;
    const std::vector<std::uint8_t> tint_frame =
        bo2::platform::headless::render_level(tinted, white_texture, tint_options);
    CHECK(plain_frame[center + 0] > 100);
    CHECK(tint_frame[center + 0] < plain_frame[center + 0] * 0.7);

    bo2::engine::Image lightmap;
    lightmap.width = 1;
    lightmap.height = 1;
    lightmap.rgba = {128, 128, 128, 255};
    textured.lightmaps = {lightmap};
    bo2::platform::headless::FrameOptions light_options = level_options;
    light_options.lightmap = true;
    const std::vector<std::uint8_t> light_frame =
        bo2::platform::headless::render_level(textured, textures, light_options);
    CHECK(light_frame[center + 0] > 0);
    CHECK(light_frame[center + 0] < textured_frame[center + 0] * 0.8);

    // With .slm vertex colours the level equation is texture x vertex colour
    // x light map: a white texture, a half-red vertex colour and a mid-grey
    // light map give about 255 * 0.5 * 0.5 in red and 255 * 0.5 in green.
    bo2::engine::LevelGeometry vertex_lit = level_quad();
    vertex_lit.texcoords = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    for (bo2::engine::LevelTriangle& triangle : vertex_lit.triangles) {
        triangle.texture = 0;
    }
    vertex_lit.lightmaps = {lightmap};
    vertex_lit.vertex_colors.assign(vertex_lit.positions.size(), bo2::engine::Vec3{0.5f, 1.0f, 1.0f});
    const std::vector<std::uint8_t> vertex_frame =
        bo2::platform::headless::render_level(vertex_lit, white_texture, light_options);
    CHECK(vertex_frame[center + 0] > 55 && vertex_frame[center + 0] < 75);
    CHECK(vertex_frame[center + 1] > 118 && vertex_frame[center + 1] < 138);

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "renderer tests: ok\n";
    return 0;
}
