#include "bo2/game/camera.hpp"
#include "bo2/game/player.hpp"
#include "bo2/game/save.hpp"
#include "bo2/engine/collision.hpp"
#include "bo2/platform/input.hpp"
#include "bo2/platform/sdl/sdl_backend.hpp"
#include "render_pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>

#include <SDL.h>
#include <SDL_opengl.h>

namespace bo2::platform::sdl {

namespace {

constexpr float kPi = 3.14159265358979323846f;

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
                detail::LevelPipeline& pipeline, const Camera& camera, std::uint32_t width,
                std::uint32_t height, engine::MaterialMode material_mode, bool use_lightmap) {
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glClearColor(20.0f / 255.0f, 22.0f / 255.0f, 30.0f / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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

    pipeline.draw(geometry, textures, lightmaps, model_lightmap_offset, matrix, material_mode,
                  use_lightmap);

    glMatrixMode(GL_PROJECTION);
    load_matrix(matrix);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
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

std::set<InputKey> pressed_keys(const Uint8* keys) {
    struct Binding {
        SDL_Scancode scancode;
        InputKey key;
    };
    static constexpr Binding bindings[] = {
        {SDL_SCANCODE_W, InputKey::W},       {SDL_SCANCODE_A, InputKey::A},
        {SDL_SCANCODE_S, InputKey::S},       {SDL_SCANCODE_D, InputKey::D},
        {SDL_SCANCODE_Q, InputKey::Q},       {SDL_SCANCODE_E, InputKey::E},
        {SDL_SCANCODE_R, InputKey::R},       {SDL_SCANCODE_F, InputKey::F},
        {SDL_SCANCODE_UP, InputKey::Up},     {SDL_SCANCODE_DOWN, InputKey::Down},
        {SDL_SCANCODE_LEFT, InputKey::Left}, {SDL_SCANCODE_RIGHT, InputKey::Right},
        {SDL_SCANCODE_SPACE, InputKey::Space}, {SDL_SCANCODE_RETURN, InputKey::Enter},
        {SDL_SCANCODE_ESCAPE, InputKey::Escape}, {SDL_SCANCODE_TAB, InputKey::Tab},
    };
    std::set<InputKey> pressed;
    for (const Binding& binding : bindings) {
        if (keys[binding.scancode] != 0) {
            pressed.insert(binding.key);
        }
    }
    return pressed;
}

std::set<InputButton> pressed_buttons(SDL_GameController* controller) {
    struct Binding {
        SDL_GameControllerButton button;
        InputButton key;
    };
    static constexpr Binding bindings[] = {
        {SDL_CONTROLLER_BUTTON_A, InputButton::A},
        {SDL_CONTROLLER_BUTTON_B, InputButton::B},
        {SDL_CONTROLLER_BUTTON_X, InputButton::X},
        {SDL_CONTROLLER_BUTTON_Y, InputButton::Y},
        {SDL_CONTROLLER_BUTTON_DPAD_UP, InputButton::Up},
        {SDL_CONTROLLER_BUTTON_DPAD_DOWN, InputButton::Down},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, InputButton::Left},
        {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, InputButton::Right},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, InputButton::L},
        {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, InputButton::R},
        {SDL_CONTROLLER_BUTTON_START, InputButton::Plus},
        {SDL_CONTROLLER_BUTTON_BACK, InputButton::Minus},
        {SDL_CONTROLLER_BUTTON_LEFTSTICK, InputButton::LStick},
        {SDL_CONTROLLER_BUTTON_RIGHTSTICK, InputButton::RStick},
    };
    std::set<InputButton> pressed;
    if (controller == nullptr || SDL_GameControllerGetAttached(controller) == SDL_FALSE) {
        return pressed;
    }
    for (const Binding& binding : bindings) {
        if (SDL_GameControllerGetButton(controller, binding.button) != 0) {
            pressed.insert(binding.key);
        }
    }
    if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16384) {
        pressed.insert(InputButton::ZL);
    }
    if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16384) {
        pressed.insert(InputButton::ZR);
    }
    return pressed;
}

void controller_axes(SDL_GameController* controller, float& move_x, float& move_y, float& look_x,
                     float& look_y) {
    move_x = 0.0f;
    move_y = 0.0f;
    look_x = 0.0f;
    look_y = 0.0f;
    if (controller == nullptr || SDL_GameControllerGetAttached(controller) == SDL_FALSE) {
        return;
    }
    const auto axis = [controller](SDL_GameControllerAxis index) {
        return static_cast<float>(SDL_GameControllerGetAxis(controller, index)) / 32767.0f;
    };
    move_x = axis(SDL_CONTROLLER_AXIS_LEFTX);
    move_y = -axis(SDL_CONTROLLER_AXIS_LEFTY);
    look_x = axis(SDL_CONTROLLER_AXIS_RIGHTX);
    look_y = axis(SDL_CONTROLLER_AXIS_RIGHTY);
}

SDL_GameController* open_controller(int index) {
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        return nullptr;
    }
    if (SDL_IsGameController(index) == SDL_FALSE) {
        return nullptr;
    }
    return SDL_GameControllerOpen(index);
}

void draw_hud(std::uint32_t width, std::uint32_t height, const game::HudState& hud) {
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(width), static_cast<double>(height), 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    const float bar_width = static_cast<float>(width) * 0.3f;
    const float bar_height = static_cast<float>(height) * 0.025f;
    const float x = static_cast<float>(width) * 0.03f;
    const float y = static_cast<float>(height) * 0.93f;
    auto bar = [&](float position, float fraction, float red, float green, float blue) {
        glColor3f(0.1f, 0.1f, 0.1f);
        glBegin(GL_QUADS);
        glVertex2f(x, y + position);
        glVertex2f(x + bar_width, y + position);
        glVertex2f(x + bar_width, y + position + bar_height);
        glVertex2f(x, y + position + bar_height);
        glEnd();
        glColor3f(red, green, blue);
        glBegin(GL_QUADS);
        glVertex2f(x, y + position);
        glVertex2f(x + bar_width * fraction, y + position);
        glVertex2f(x + bar_width * fraction, y + position + bar_height);
        glVertex2f(x, y + position + bar_height);
        glEnd();
    };
    bar(0.0f, std::clamp(hud.health / 100.0f, 0.0f, 1.0f), 0.85f, 0.15f, 0.15f);
    bar(bar_height * 1.6f, std::clamp(hud.mana / 100.0f, 0.0f, 1.0f), 0.15f, 0.3f, 0.9f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
}

void draw_player(const engine::Vec3& position) {
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glColor3f(0.95f, 0.85f, 0.2f);
    glVertex3f(position.x, position.y, position.z);
    glVertex3f(position.x, position.y, position.z + 1.8f);
    glVertex3f(position.x - 0.3f, position.y, position.z);
    glVertex3f(position.x + 0.3f, position.y, position.z);
    glVertex3f(position.x, position.y - 0.3f, position.z);
    glVertex3f(position.x, position.y + 0.3f, position.z);
    glEnd();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
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
    detail::LevelPipeline pipeline;
    pipeline.upload(geometry);
    OrbitCamera orbit = make_orbit(options.camera);
    std::unique_ptr<engine::CollisionWorld> collision;
    std::unique_ptr<game::PlayerController> player;
    std::unique_ptr<game::FollowCamera> follow;
    if (options.play) {
        collision = std::make_unique<engine::CollisionWorld>(geometry);
        engine::Vec3 start = options.camera.target;
        start.z += 1.0f;
        player = std::make_unique<game::PlayerController>(*collision, start);
        follow = std::make_unique<game::FollowCamera>(4.0f);
        follow->update(player->state(), *collision);
    }
    const auto active_camera = [&]() {
        if (follow) {
            Camera value;
            value.eye = follow->eye();
            value.target = follow->target();
            value.up = {0.0f, 0.0f, 1.0f};
            value.fov_degrees = options.camera.fov_degrees;
            return value;
        }
        return current_camera(orbit);
    };
    if (!options.screenshot.empty()) {
        const auto frame_start = std::chrono::steady_clock::now();
        draw_level(geometry, texture_ids, lightmap_ids, model_lightmap_offset, pipeline,
                   active_camera(), options.width, options.height, options.material_mode,
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

    SDL_GameController* controller = nullptr;
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0) {
        for (int index = 0; index < SDL_NumJoysticks(); ++index) {
            if (SDL_IsGameController(index) != SDL_FALSE) {
                controller = SDL_GameControllerOpen(index);
                if (controller != nullptr) {
                    break;
                }
            }
        }
    }
    const InputMap input = default_host_map();
    bool running = true;
    bool dragging = false;
    bool print_stats = options.stats;
    Uint32 last_ticks = SDL_GetTicks();
    std::uint64_t play_tick = 0;
    game::CheckpointSlots checkpoints;
    game::HudState hud;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            } else if (event.type == SDL_CONTROLLERDEVICEADDED && controller == nullptr) {
                controller = open_controller(event.cdevice.which);
            } else if (event.type == SDL_CONTROLLERDEVICEREMOVED && controller != nullptr &&
                       event.cdevice.which ==
                           SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))) {
                SDL_GameControllerClose(controller);
                controller = nullptr;
            } else if (player != nullptr && event.type == SDL_KEYDOWN &&
                       event.key.keysym.sym == SDLK_F5) {
                game::SaveGame save;
                save.level = options.title;
                save.player = player->state();
                save.hud = hud;
                save.tick = play_tick;
                checkpoints.store(0, save);
                std::cout << "checkpoint saved\n";
            } else if (player != nullptr && event.type == SDL_KEYDOWN &&
                       event.key.keysym.sym == SDLK_F9) {
                const auto save = checkpoints.load(0);
                if (save.has_value()) {
                    player->restore(save->player);
                    hud = save->hud;
                    follow->update(player->state(), *collision);
                    std::cout << "checkpoint loaded\n";
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
                dragging = true;
            } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_RIGHT) {
                dragging = false;
            } else if (event.type == SDL_MOUSEMOTION && dragging) {
                if (player) {
                    player->look(-static_cast<float>(event.motion.xrel) * 0.005f,
                                 static_cast<float>(event.motion.yrel) * 0.005f);
                } else {
                    orbit.yaw -= static_cast<float>(event.motion.xrel) * 0.005f;
                    orbit.pitch += static_cast<float>(event.motion.yrel) * 0.005f;
                    orbit.pitch = std::clamp(orbit.pitch, -1.5f, 1.5f);
                }
            } else if (event.type == SDL_MOUSEWHEEL && player == nullptr) {
                orbit.distance *= 1.0f - static_cast<float>(event.wheel.y) * 0.1f;
                orbit.distance = std::max(0.1f, orbit.distance);
            }
        }
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        float move_x = 0.0f;
        float move_y = 0.0f;
        float look_x = 0.0f;
        float look_y = 0.0f;
        controller_axes(controller, move_x, move_y, look_x, look_y);
        const InputState state =
            input.update(pressed_keys(keys), pressed_buttons(controller), move_x, move_y, look_x,
                         look_y);
        if (state.action(InputAction::Pause)) {
            running = false;
        }
        const float forward_axis =
            std::clamp((state.action(InputAction::MoveForward) ? 1.0f : 0.0f) -
                           (state.action(InputAction::MoveBack) ? 1.0f : 0.0f) + state.move_y,
                       -1.0f, 1.0f);
        const float right_axis =
            std::clamp((state.action(InputAction::MoveRight) ? 1.0f : 0.0f) -
                           (state.action(InputAction::MoveLeft) ? 1.0f : 0.0f) + state.move_x,
                       -1.0f, 1.0f);
        if (player) {
            const Uint32 now = SDL_GetTicks();
            const float dt = std::clamp(static_cast<float>(now - last_ticks) / 1000.0f, 0.0f, 0.1f);
            last_ticks = now;
            game::PlayerInput player_input;
            player_input.move_x = right_axis;
            player_input.move_y = forward_axis;
            player_input.look_x = state.look_x;
            player_input.look_y = state.look_y;
            player_input.run = state.action(InputAction::Run);
            player_input.jump = state.action(InputAction::Jump);
            player->step(player_input, static_cast<double>(dt));
            follow->update(player->state(), *collision);
            ++play_tick;
        } else {
            const float step = std::max(0.01f, orbit.distance * 0.01f) *
                               (state.action(InputAction::Run) ? 2.0f : 1.0f);
            const engine::Vec3 view = engine::normalize(orbit.target - current_camera(orbit).eye);
            engine::Vec3 forward{view.x, view.y, 0.0f};
            if (engine::length(forward) < 1.0e-6f) {
                forward = {1.0f, 0.0f, 0.0f};
            } else {
                forward = engine::normalize(forward);
            }
            const engine::Vec3 right =
                engine::normalize(engine::cross(forward, {0.0f, 0.0f, 1.0f}));
            orbit.target =
                orbit.target + forward * (forward_axis * step) + right * (right_axis * step);
            orbit.yaw -= state.look_x * 0.05f;
            orbit.pitch += state.look_y * 0.05f;
            if (state.action(InputAction::LookUp)) {
                orbit.pitch += 0.02f;
            }
            if (state.action(InputAction::LookDown)) {
                orbit.pitch -= 0.02f;
            }
            if (state.action(InputAction::LookLeft)) {
                orbit.yaw += 0.02f;
            }
            if (state.action(InputAction::LookRight)) {
                orbit.yaw -= 0.02f;
            }
            orbit.pitch = std::clamp(orbit.pitch, -1.5f, 1.5f);
        }
        const auto frame_start = std::chrono::steady_clock::now();
        draw_level(geometry, texture_ids, lightmap_ids, model_lightmap_offset, pipeline,
                   active_camera(), options.width, options.height, options.material_mode,
                   options.lightmap);
        draw_player(player ? player->state().position : orbit.target);
        if (player) {
            draw_hud(options.width, options.height, hud);
        }
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
    if (controller != nullptr) {
        SDL_GameControllerClose(controller);
    }
    return 0;
}

}  // namespace bo2::platform::sdl
