#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bo2/engine/cell_group.hpp"
#include "bo2/engine/image.hpp"
#include "bo2/engine/math.hpp"

namespace bo2::platform::headless {

struct Camera {
    engine::Vec3 eye{2.4f, 1.6f, 2.4f};
    engine::Vec3 target{0.0f, 0.0f, 0.0f};
    engine::Vec3 up{0.0f, 1.0f, 0.0f};
    float fov_degrees = 55.0f;
};

struct FrameOptions {
    std::uint32_t width = 640;
    std::uint32_t height = 480;
    Camera camera;
};

std::vector<std::uint8_t> render_textured_cube(const engine::Image& texture,
                                               const FrameOptions& options);
std::vector<std::uint8_t> render_level(const engine::LevelGeometry& geometry,
                                       const FrameOptions& options);
void write_ppm(const std::string& path, std::uint32_t width, std::uint32_t height,
               const std::vector<std::uint8_t>& rgba);

}  // namespace bo2::platform::headless
