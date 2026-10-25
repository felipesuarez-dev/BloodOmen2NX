#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bo2/engine/cell_group.hpp"
#include "bo2/engine/image.hpp"
#include "bo2/engine/math.hpp"

namespace bo2::platform::sdl {

struct Camera {
    engine::Vec3 eye{2.4f, 1.6f, 2.4f};
    engine::Vec3 target{0.0f, 0.0f, 0.0f};
    engine::Vec3 up{0.0f, 0.0f, 1.0f};
    float fov_degrees = 55.0f;
};

struct WindowOptions {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::string title = "Blood Omen 2 NX";
    std::string screenshot;
    bool hidden = false;
    Camera camera;
};

int run_triangle(const WindowOptions& options);
int run_level(const engine::LevelGeometry& geometry,
              const std::vector<engine::Image>& textures, const WindowOptions& options);

}  // namespace bo2::platform::sdl
