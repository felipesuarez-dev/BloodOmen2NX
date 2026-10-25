#include "bo2/platform/sdl/sdl_backend.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL.h>
#include <SDL_opengl.h>

namespace bo2::platform::sdl {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kLightX = -0.35f;
constexpr float kLightY = 0.55f;
constexpr float kLightZ = 0.75f;

void check_sdl(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(std::string(what) + ": " + SDL_GetError());
    }
}

struct Context {
    SDL_Window* window = nullptr;
    SDL_GLContext gl = nullptr;

    ~Context() {
        if (gl != nullptr) {
            SDL_GL_DeleteContext(gl);
        }
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
            SDL_Quit();
        }
    }
};

std::unique_ptr<Context> create_context(const WindowOptions& options, const std::string& title) {
    check_sdl(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL_Init");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    Uint32 flags = SDL_WINDOW_OPENGL;
    if (options.hidden) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    auto context = std::make_unique<Context>();
    context->window = SDL_CreateWindow(
        title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        static_cast<int>(options.width), static_cast<int>(options.height), flags);
    check_sdl(context->window != nullptr, "SDL_CreateWindow");
    context->gl = SDL_GL_CreateContext(context->window);
    check_sdl(context->gl != nullptr, "SDL_GL_CreateContext");
    SDL_GL_MakeCurrent(context->window, context->gl);
    SDL_GL_SetSwapInterval(0);
    return context;
}

void load_matrix(const engine::Mat4& matrix) {
    GLfloat values[16];
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            values[column * 4 + row] = matrix.m[row * 4 + column];
        }
    }
    glLoadMatrixf(values);
}

float shade_of(const engine::Vec3& normal) {
    const engine::Vec3 light =
        engine::normalize({kLightX, kLightY, kLightZ});
    const engine::Vec3 unit = engine::normalize(normal);
    if (engine::length(unit) < 0.5f) {
        return 0.85f;
    }
    return 0.25f + 0.75f * std::fabs(engine::dot(unit, light));
}

engine::Vec3 material_color(const engine::LevelGeometry& geometry,
                            const engine::LevelTriangle& triangle) {
    static const engine::Vec3 palette[8] = {
        {0.62f, 0.56f, 0.48f}, {0.50f, 0.52f, 0.56f}, {0.58f, 0.47f, 0.38f},
        {0.45f, 0.50f, 0.43f}, {0.54f, 0.44f, 0.42f}, {0.60f, 0.56f, 0.44f},
        {0.42f, 0.46f, 0.52f}, {0.52f, 0.48f, 0.58f},
    };
    const std::size_t index = static_cast<std::size_t>(triangle.material);
    if (index < geometry.materials.size() && geometry.materials[index].has_colors) {
        engine::Vec3 color = geometry.materials[index].colors[1];
        if (geometry.materials[index].has_controls) {
            const float weight = geometry.materials[index].controls[1].weight;
            if (weight >= 0.0f && weight <= 1.0f) {
                color = color * weight;
            }
        }
        return color;
    }
    return palette[index % 8];
}

std::vector<GLuint> upload_textures(const std::vector<engine::Image>& textures) {
    std::vector<GLuint> ids(textures.size(), 0);
    if (!ids.empty()) {
        glGenTextures(static_cast<GLsizei>(ids.size()), ids.data());
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (std::size_t index = 0; index < textures.size(); ++index) {
        const engine::Image& image = textures[index];
        if (image.width == 0 || image.height == 0 ||
            image.rgba.size() < static_cast<std::size_t>(image.width) * image.height * 4) {
            ids[index] = 0;
            continue;
        }
        glBindTexture(GL_TEXTURE_2D, ids[index]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(image.width),
                     static_cast<GLsizei>(image.height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     image.rgba.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return ids;
}

engine::Mat4 view_projection(const Camera& camera, float aspect, float near_plane,
                             float far_plane) {
    if (camera.fov_degrees <= 1.0f || camera.fov_degrees >= 179.0f) {
        throw std::runtime_error("unsupported field of view");
    }
    engine::Vec3 forward = camera.target - camera.eye;
    if (engine::length(forward) < 1.0e-6f) {
        throw std::runtime_error("camera eye and target are the same");
    }
    engine::Vec3 up = camera.up;
    if (engine::length(up) < 1.0e-6f) {
        up = {0.0f, 1.0f, 0.0f};
    }
    if (engine::length(engine::cross(engine::normalize(forward), engine::normalize(up))) < 1.0e-6f) {
        up = {0.0f, 0.0f, 1.0f};
    }
    const engine::Mat4 view = engine::Mat4::look_at(camera.eye, camera.target, up);
    const engine::Mat4 projection =
        engine::Mat4::perspective(camera.fov_degrees * kPi / 180.0f, aspect, near_plane, far_plane);
    return projection * view;
}

void draw_level(const engine::LevelGeometry& geometry, const std::vector<GLuint>& textures,
                const std::vector<GLuint>& lightmaps, std::size_t model_lightmap_offset,
                const Camera& camera, std::uint32_t width, std::uint32_t height,
                engine::MaterialMode material_mode, bool use_lightmap) {
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glClearColor(20.0f / 255.0f, 22.0f / 255.0f, 30.0f / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    engine::Vec3 minimum = geometry.minimum;
    engine::Vec3 maximum = geometry.maximum;
    if (geometry.positions.empty()) {
        minimum = {0.0f, 0.0f, 0.0f};
        maximum = {0.0f, 0.0f, 0.0f};
    }
    const float distance = engine::length(camera.eye - camera.target);
    const float extent =
        std::max({maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z});
    const float near_plane = std::max(0.05f, distance * 0.001f);
    const float far_plane = distance + extent * 2.0f + 10.0f;
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const engine::Mat4 matrix = view_projection(camera, aspect, near_plane, far_plane);

    glMatrixMode(GL_PROJECTION);
    load_matrix(matrix);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    for (const engine::LevelTriangle& triangle : geometry.triangles) {
        if (triangle.a >= geometry.positions.size() || triangle.b >= geometry.positions.size() ||
            triangle.c >= geometry.positions.size()) {
            continue;
        }
        const engine::Vec3 positions[3] = {geometry.positions[triangle.a],
                                           geometry.positions[triangle.b],
                                           geometry.positions[triangle.c]};
        const engine::Vec3 normals[3] = {geometry.normals[triangle.a], geometry.normals[triangle.b],
                                         geometry.normals[triangle.c]};
        const bool textured = triangle.texture >= 0 &&
                              static_cast<std::size_t>(triangle.texture) < textures.size() &&
                              textures[static_cast<std::size_t>(triangle.texture)] != 0;
        const engine::Vec2 uv[3] = {
            (textured && triangle.a < geometry.texcoords.size()) ? geometry.texcoords[triangle.a]
                                                                 : engine::Vec2{},
            (textured && triangle.b < geometry.texcoords.size()) ? geometry.texcoords[triangle.b]
                                                                 : engine::Vec2{},
            (textured && triangle.c < geometry.texcoords.size()) ? geometry.texcoords[triangle.c]
                                                                 : engine::Vec2{},
        };
        const engine::Vec3 tint = engine::material_tint(geometry, triangle, material_mode);
        GLuint lightmap_id = 0;
        if (use_lightmap && textured) {
            const std::size_t model_index =
                triangle.lightmap >= 0
                    ? model_lightmap_offset + static_cast<std::size_t>(triangle.lightmap)
                    : lightmaps.size();
            if (model_index < lightmaps.size()) {
                lightmap_id = lightmaps[model_index];
            } else if (static_cast<std::size_t>(triangle.texture) < model_lightmap_offset) {
                lightmap_id = lightmaps[static_cast<std::size_t>(triangle.texture)];
            }
        }
        const engine::Vec2 light_uv[3] = {
            (textured && triangle.a < geometry.lightmap_texcoords.size())
                ? geometry.lightmap_texcoords[triangle.a]
                : uv[0],
            (textured && triangle.b < geometry.lightmap_texcoords.size())
                ? geometry.lightmap_texcoords[triangle.b]
                : uv[1],
            (textured && triangle.c < geometry.lightmap_texcoords.size())
                ? geometry.lightmap_texcoords[triangle.c]
                : uv[2],
        };
        if (textured) {
            glActiveTexture(GL_TEXTURE0);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textures[static_cast<std::size_t>(triangle.texture)]);
        } else {
            glDisable(GL_TEXTURE_2D);
            const engine::Vec3 color = material_color(geometry, triangle);
            glColor3f(color.x, color.y, color.z);
        }
        if (lightmap_id != 0) {
            glActiveTexture(GL_TEXTURE1);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, lightmap_id);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        }
        glBegin(GL_TRIANGLES);
        for (int corner = 0; corner < 3; ++corner) {
            const float shade = shade_of(normals[corner]);
            if (textured) {
                const float light = lightmap_id != 0 ? 1.0f : shade;
                glColor4f(light * tint.x, light * tint.y, light * tint.z, 1.0f);
                glMultiTexCoord2f(GL_TEXTURE0, uv[corner].x, uv[corner].y);
                if (lightmap_id != 0) {
                    glMultiTexCoord2f(GL_TEXTURE1, light_uv[corner].x, light_uv[corner].y);
                }
            }
            glVertex3f(positions[corner].x, positions[corner].y, positions[corner].z);
        }
        glEnd();
        if (lightmap_id != 0) {
            glActiveTexture(GL_TEXTURE1);
            glDisable(GL_TEXTURE_2D);
        }
        glActiveTexture(GL_TEXTURE0);
    }
    glDisable(GL_TEXTURE_2D);
}

void write_ppm(const std::string& path, std::uint32_t width, std::uint32_t height,
               const std::vector<std::uint8_t>& rgba) {
    if (rgba.size() < static_cast<std::size_t>(width) * height * 4) {
        throw std::runtime_error("framebuffer is smaller than the frame");
    }
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot write " + path);
    }
    stream << "P6\n" << width << ' ' << height << "\n255\n";
    for (std::uint32_t row = 0; row < height; ++row) {
        const std::uint32_t source = height - 1 - row;
        for (std::uint32_t column = 0; column < width; ++column) {
            const std::size_t offset = (static_cast<std::size_t>(source) * width + column) * 4;
            stream.write(reinterpret_cast<const char*>(rgba.data() + offset), 3);
        }
    }
}

void capture(const std::string& path, std::uint32_t width, std::uint32_t height) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    glReadPixels(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels.data());
    write_ppm(path, width, height, pixels);
    std::cout << "sdl frame: " << path << " (" << width << 'x' << height << ")\n";
}

struct OrbitCamera {
    engine::Vec3 target{0.0f, 0.0f, 0.0f};
    float distance = 10.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fov = 55.0f;
};

OrbitCamera make_orbit(const Camera& camera) {
    OrbitCamera orbit;
    orbit.target = camera.target;
    orbit.fov = camera.fov_degrees;
    const engine::Vec3 offset = camera.eye - camera.target;
    orbit.distance = std::max(0.01f, engine::length(offset));
    orbit.yaw = std::atan2(offset.y, offset.x);
    orbit.pitch = std::asin(std::clamp(offset.z / orbit.distance, -1.0f, 1.0f));
    return orbit;
}

Camera current_camera(const OrbitCamera& orbit) {
    Camera camera;
    const float cos_pitch = std::cos(orbit.pitch);
    camera.eye = orbit.target + engine::Vec3{orbit.distance * cos_pitch * std::cos(orbit.yaw),
                                             orbit.distance * cos_pitch * std::sin(orbit.yaw),
                                             orbit.distance * std::sin(orbit.pitch)};
    camera.target = orbit.target;
    camera.up = {0.0f, 0.0f, 1.0f};
    camera.fov_degrees = orbit.fov;
    return camera;
}

void draw_triangle(std::uint32_t width, std::uint32_t height, float angle) {
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glClearColor(20.0f / 255.0f, 22.0f / 255.0f, 30.0f / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(angle, 0.0f, 0.0f, 1.0f);
    glBegin(GL_TRIANGLES);
    glColor3f(0.9f, 0.2f, 0.2f);
    glVertex2f(-0.6f, -0.5f);
    glColor3f(0.2f, 0.9f, 0.3f);
    glVertex2f(0.6f, -0.5f);
    glColor3f(0.2f, 0.4f, 0.9f);
    glVertex2f(0.0f, 0.6f);
    glEnd();
}

}  // namespace

int run_triangle(const WindowOptions& options) {
    auto context = create_context(options, options.title + " (triangle)");
    if (!options.screenshot.empty()) {
        draw_triangle(options.width, options.height, 0.0f);
        glFinish();
        capture(options.screenshot, options.width, options.height);
        return 0;
    }
    bool running = true;
    const Uint32 start = SDL_GetTicks();
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
        }
        const float angle = static_cast<float>(SDL_GetTicks() - start) * 0.05f;
        draw_triangle(options.width, options.height, angle);
        SDL_GL_SwapWindow(context->window);
        SDL_Delay(16);
    }
    return 0;
}

int run_level(const engine::LevelGeometry& geometry,
              const std::vector<engine::Image>& textures, const WindowOptions& options) {
    auto context = create_context(options, options.title);
    const std::vector<GLuint> texture_ids = upload_textures(textures);
    std::vector<engine::Image> combined_lightmaps = geometry.lightmaps;
    const std::size_t model_lightmap_offset = combined_lightmaps.size();
    combined_lightmaps.insert(combined_lightmaps.end(), geometry.model_lightmaps.begin(),
                              geometry.model_lightmaps.end());
    const std::vector<GLuint> lightmap_ids = upload_textures(combined_lightmaps);
    OrbitCamera orbit = make_orbit(options.camera);
    if (!options.screenshot.empty()) {
        const auto frame_start = std::chrono::steady_clock::now();
        draw_level(geometry, texture_ids, lightmap_ids, model_lightmap_offset,
                   current_camera(orbit), options.width, options.height, options.material_mode,
                   options.lightmap);
        glFinish();
        capture(options.screenshot, options.width, options.height);
        if (options.stats) {
            const double frame_ms = static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - frame_start)
                    .count()) /
                1000.0;
            std::cout << "sdl stats: frame " << frame_ms << " ms\n";
        }
        return 0;
    }

    bool running = true;
    bool dragging = false;
    bool print_stats = options.stats;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
                dragging = true;
            } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_RIGHT) {
                dragging = false;
            } else if (event.type == SDL_MOUSEMOTION && dragging) {
                orbit.yaw -= static_cast<float>(event.motion.xrel) * 0.005f;
                orbit.pitch += static_cast<float>(event.motion.yrel) * 0.005f;
                orbit.pitch = std::clamp(orbit.pitch, -1.5f, 1.5f);
            } else if (event.type == SDL_MOUSEWHEEL) {
                orbit.distance *= 1.0f - static_cast<float>(event.wheel.y) * 0.1f;
                orbit.distance = std::max(0.1f, orbit.distance);
            }
        }
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        const float step = std::max(0.01f, orbit.distance * 0.01f);
        const engine::Vec3 view = engine::normalize(orbit.target - current_camera(orbit).eye);
        engine::Vec3 forward{view.x, view.y, 0.0f};
        if (engine::length(forward) < 1.0e-6f) {
            forward = {1.0f, 0.0f, 0.0f};
        } else {
            forward = engine::normalize(forward);
        }
        const engine::Vec3 right = engine::normalize(engine::cross(forward, {0.0f, 0.0f, 1.0f}));
        if (keys[SDL_SCANCODE_W] != 0 || keys[SDL_SCANCODE_UP] != 0) {
            orbit.target = orbit.target + forward * step;
        }
        if (keys[SDL_SCANCODE_S] != 0 || keys[SDL_SCANCODE_DOWN] != 0) {
            orbit.target = orbit.target - forward * step;
        }
        if (keys[SDL_SCANCODE_D] != 0 || keys[SDL_SCANCODE_RIGHT] != 0) {
            orbit.target = orbit.target + right * step;
        }
        if (keys[SDL_SCANCODE_A] != 0 || keys[SDL_SCANCODE_LEFT] != 0) {
            orbit.target = orbit.target - right * step;
        }
        if (keys[SDL_SCANCODE_E] != 0) {
            orbit.target.z += step;
        }
        if (keys[SDL_SCANCODE_Q] != 0) {
            orbit.target.z -= step;
        }
        const auto frame_start = std::chrono::steady_clock::now();
        draw_level(geometry, texture_ids, lightmap_ids, model_lightmap_offset,
                   current_camera(orbit), options.width, options.height, options.material_mode,
                   options.lightmap);
        SDL_GL_SwapWindow(context->window);
        if (print_stats) {
            const double frame_ms = static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - frame_start)
                    .count()) /
                1000.0;
            std::cout << "sdl stats: frame " << frame_ms << " ms\n";
            print_stats = false;
        }
        SDL_Delay(1);
    }
    return 0;
}

}  // namespace bo2::platform::sdl
