#include <algorithm>
#include <cctype>
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/cell_group.hpp"
#include "bo2/engine/collision.hpp"
#include "bo2/engine/dds.hpp"
#include "bo2/engine/erg.hpp"
#include "bo2/engine/navigation.hpp"
#include "bo2/engine/ogg.hpp"
#include "bo2/engine/tick.hpp"
#include "bo2/engine/world.hpp"
#include "bo2/platform/platform.hpp"
#include "bo2/platform/headless/software_renderer.hpp"
#include "bo2/version.hpp"
#ifdef BO2NX_HAVE_SDL
#include "bo2/platform/sdl/audio.hpp"
#include "bo2/platform/sdl/sdl_backend.hpp"
#endif

namespace {

struct Options {
    std::string list_big;
    std::string erg;
    bool frame = false;
    bool level = false;
    bool sdl = false;
    bool headless = false;
    bool stats = false;
    std::uint32_t frames = 0;
    std::uint32_t tick_rate = 60;
    bool eye_set = false;
    bool target_set = false;
    std::string big;
    std::string entry;
    std::string out;
    std::string event_ogg;
    std::string music_ogg;
    float audio_seconds = 5.0f;
    bo2::platform::headless::FrameOptions frame_options;
};

[[noreturn]] void usage_error(const std::string& message) {
    throw std::runtime_error(message + "\nTry --help.");
}

std::string next_argument(int& index, int argc, char** argv) {
    ++index;
    if (index >= argc) {
        usage_error("missing argument");
    }
    return argv[index];
}

float parse_float(const std::string& text, const char* name) {
    std::size_t consumed = 0;
    const float value = std::stof(text, &consumed);
    if (consumed != text.size()) {
        usage_error(std::string("invalid ") + name + ": " + text);
    }
    return value;
}

std::uint32_t parse_dimension(const std::string& text, const char* name) {
    std::size_t consumed = 0;
    const int value = std::stoi(text, &consumed);
    if (consumed != text.size() || value <= 0 || value > 8192) {
        usage_error(std::string("invalid ") + name + ": " + text);
    }
    return static_cast<std::uint32_t>(value);
}

bo2::engine::MaterialMode parse_material_mode(const std::string& text) {
    if (text == "off") {
        return bo2::engine::MaterialMode::Off;
    }
    if (text == "diffuse") {
        return bo2::engine::MaterialMode::Diffuse;
    }
    if (text == "layers") {
        return bo2::engine::MaterialMode::Layers;
    }
    if (text == "layers2x") {
        return bo2::engine::MaterialMode::Layers2x;
    }
    usage_error("invalid material mode: " + text);
}

bo2::engine::Vec3 parse_vec3(const std::string& text, const char* name) {
    const std::size_t first = text.find(',');
    const std::size_t second = first == std::string::npos ? std::string::npos : text.find(',', first + 1);
    if (first == std::string::npos || second == std::string::npos ||
        text.find(',', second + 1) != std::string::npos) {
        usage_error(std::string("invalid ") + name + ": " + text);
    }
    return {parse_float(text.substr(0, first), name),
            parse_float(text.substr(first + 1, second - first - 1), name),
            parse_float(text.substr(second + 1), name)};
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            std::cout
                << "Usage:\n"
                << "  bo2nx\n"
                << "  bo2nx --list-big <file.big>\n"
                << "  bo2nx --erg <game.erg>\n"
                << "  bo2nx --frame --big <file.big> --out <frame.ppm> [options]\n"
                << "  bo2nx --level --big <file.big> --out <frame.ppm> [options]\n"
                << "  bo2nx --sdl [--big <file.big>] [--out <screenshot.ppm>] [options]\n"
                << "  bo2nx --event-ogg <event.ogg> [--music-ogg <music.ogg>] [--audio-seconds n]\n"
                << "  bo2nx --headless --frames <n> [--tick-rate <hz>]\n\n"
                << "Frame options:\n"
                << "  --entry <substring>   texture entry (default: first texture)\n"
                << "  --width <n>           frame width (default 640)\n"
                << "  --height <n>          frame height (default 480)\n"
                << "  --eye x,y,z           camera position (default 2.4,1.6,2.4)\n"
                << "  --target x,y,z        camera target (default 0,0,0)\n"
                << "  --fov <degrees>       vertical field of view (default 55)\n"
                << "  --material <mode>     material tint: off, diffuse, layers, layers2x\n"
                << "  --lightmap            sample the .slm light maps on textured surfaces\n"
                << "  --stats               print load, parse, render and memory numbers\n"
                << "  --event-ogg <file>     play one Ogg event\n"
                << "  --music-ogg <file>     loop an Ogg music track\n"
                << "  --audio-seconds <n>    audio demo duration (default 5)\n"
                << "  --headless             run the fixed-step loop without a window\n"
                << "  --frames <n>           fixed-step iterations for --headless\n"
                << "  --tick-rate <hz>       logic ticks per second (default 60)\n";
            std::exit(0);
        }
        if (argument == "--list-big") {
            options.list_big = next_argument(index, argc, argv);
        } else if (argument == "--erg") {
            options.erg = next_argument(index, argc, argv);
        } else if (argument == "--frame") {
            options.frame = true;
        } else if (argument == "--level") {
            options.level = true;
        } else if (argument == "--sdl") {
            options.sdl = true;
        } else if (argument == "--headless") {
            options.headless = true;
        } else if (argument == "--frames") {
            options.frames = parse_dimension(next_argument(index, argc, argv), "frames");
        } else if (argument == "--tick-rate") {
            options.tick_rate = parse_dimension(next_argument(index, argc, argv), "tick-rate");
            if (options.tick_rate > 1000) {
                usage_error("tick-rate must be at most 1000");
            }
        } else if (argument == "--big") {
            options.big = next_argument(index, argc, argv);
        } else if (argument == "--entry") {
            options.entry = next_argument(index, argc, argv);
        } else if (argument == "--out") {
            options.out = next_argument(index, argc, argv);
        } else if (argument == "--width") {
            options.frame_options.width = parse_dimension(next_argument(index, argc, argv), "width");
        } else if (argument == "--height") {
            options.frame_options.height = parse_dimension(next_argument(index, argc, argv), "height");
        } else if (argument == "--eye") {
            options.frame_options.camera.eye = parse_vec3(next_argument(index, argc, argv), "eye");
            options.eye_set = true;
        } else if (argument == "--target") {
            options.frame_options.camera.target = parse_vec3(next_argument(index, argc, argv), "target");
            options.target_set = true;
        } else if (argument == "--fov") {
            options.frame_options.camera.fov_degrees =
                parse_float(next_argument(index, argc, argv), "fov");
        } else if (argument == "--material") {
            options.frame_options.material_mode =
                parse_material_mode(next_argument(index, argc, argv));
        } else if (argument == "--lightmap") {
            options.frame_options.lightmap = true;
        } else if (argument == "--stats") {
            options.stats = true;
        } else if (argument == "--event-ogg") {
            options.event_ogg = next_argument(index, argc, argv);
        } else if (argument == "--music-ogg") {
            options.music_ogg = next_argument(index, argc, argv);
        } else if (argument == "--audio-seconds") {
            options.audio_seconds = parse_float(next_argument(index, argc, argv), "audio-seconds");
            if (options.audio_seconds <= 0.0f || options.audio_seconds > 600.0f) {
                usage_error("audio-seconds must be between 0 and 600");
            }
        } else {
            usage_error("unknown argument: " + argument);
        }
    }
    const bool audio = !options.event_ogg.empty() || !options.music_ogg.empty();
    const int modes = (!options.list_big.empty() ? 1 : 0) + (!options.erg.empty() ? 1 : 0) +
                      (options.frame ? 1 : 0) + (options.level ? 1 : 0) +
                      (options.sdl ? 1 : 0) + (options.headless ? 1 : 0) + (audio ? 1 : 0);
    if (modes > 1) {
        usage_error("--list-big, --erg, --frame, --level, --sdl, --headless and audio are mutually exclusive");
    }
    return options;
}

const bo2::engine::BigEntry* pick_texture(const bo2::engine::BigArchive& archive,
                                          const std::string& needle) {
    const bo2::engine::BigEntry* first = nullptr;
    for (const bo2::engine::BigEntry& entry : archive.entries()) {
        if (!bo2::engine::iequals(entry.type, "texture")) {
            continue;
        }
        if (first == nullptr) {
            first = &entry;
        }
        if (!needle.empty() && bo2::engine::contains_ci(entry.name, needle)) {
            return &entry;
        }
    }
    return needle.empty() ? first : nullptr;
}

struct LoadedLevel {
    bo2::engine::LevelGeometry geometry;
    std::vector<bo2::engine::Image> textures;
    std::size_t loaded_textures = 0;
    std::size_t loaded_lightmaps = 0;
    std::size_t lightmap_entries = 0;
    std::string name;
    double load_ms = 0.0;
    double archive_ms = 0.0;
    double parse_ms = 0.0;
    double textures_ms = 0.0;
    double lightmaps_ms = 0.0;
};

double elapsed_ms(std::uint64_t start) {
    return static_cast<double>(bo2::platform::monotonic_nanoseconds() - start) / 1.0e6;
}

std::string normalize_texture_name(const std::string& name) {
    std::string result;
    result.reserve(name.size());
    for (char character : name) {
        if (character == '/') {
            result.push_back('-');
        } else {
            result.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(character))));
        }
    }
    return result;
}

LoadedLevel load_level(const Options& options) {
    if (options.big.empty()) {
        usage_error("--level requires --big");
    }
    const std::uint64_t load_start = bo2::platform::monotonic_nanoseconds();
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::load(options.big);
    const double archive_ms = elapsed_ms(load_start);
    const bo2::engine::BigEntry* entry = nullptr;
    for (const bo2::engine::BigEntry& candidate : archive.entries()) {
        if (bo2::engine::iequals(candidate.type, "cellgrup")) {
            entry = &candidate;
            break;
        }
    }
    if (entry == nullptr) {
        throw std::runtime_error("no cellgrup entry in " + options.big);
    }
    LoadedLevel level;
    level.name = entry->name;
    level.archive_ms = archive_ms;
    const std::uint64_t parse_start = bo2::platform::monotonic_nanoseconds();
    level.geometry = bo2::engine::parse_cell_group(archive.extract(*entry), entry->symbols,
                                                   archive.names());
    level.parse_ms = elapsed_ms(parse_start);
    const std::uint64_t textures_start = bo2::platform::monotonic_nanoseconds();
    level.textures.reserve(level.geometry.texture_names.size());
    for (const std::string& name : level.geometry.texture_names) {
        const bo2::engine::BigEntry* texture_entry = archive.find("texture", name);
        if (texture_entry == nullptr) {
            level.textures.emplace_back();
            continue;
        }
        try {
            level.textures.push_back(bo2::engine::decode_dds(archive.extract(*texture_entry)));
            ++level.loaded_textures;
        } catch (const std::exception&) {
            level.textures.emplace_back();
        }
    }
    level.textures_ms = elapsed_ms(textures_start);
    const std::uint64_t lightmaps_start = bo2::platform::monotonic_nanoseconds();
    const bo2::engine::BigEntry* litedata = nullptr;
    for (const bo2::engine::BigEntry& candidate : archive.entries()) {
        if (bo2::engine::iequals(candidate.type, "litedata")) {
            litedata = &candidate;
            break;
        }
    }
    std::vector<bo2::engine::LevelLightMap> lightmap_entries;
    if (litedata != nullptr) {
        lightmap_entries = bo2::engine::parse_lightmaps(archive.extract(*litedata));
        level.lightmap_entries = lightmap_entries.size();
    }
    bo2::engine::assign_model_lightmaps(level.geometry, lightmap_entries);
    level.geometry.lightmaps.reserve(level.geometry.texture_names.size());
    for (const std::string& name : level.geometry.texture_names) {
        const std::string normalized = normalize_texture_name(name);
        const bo2::engine::LevelLightMap* match = nullptr;
        for (const bo2::engine::LevelLightMap& lightmap_entry : lightmap_entries) {
            if (lightmap_entry.name.size() >= normalized.size() &&
                lightmap_entry.name.compare(
                    lightmap_entry.name.size() - normalized.size(), normalized.size(),
                    normalized) == 0) {
                match = &lightmap_entry;
                break;
            }
        }
        if (match != nullptr) {
            level.geometry.lightmaps.push_back(match->image);
            ++level.loaded_lightmaps;
        } else {
            level.geometry.lightmaps.emplace_back();
        }
    }
    level.lightmaps_ms = elapsed_ms(lightmaps_start);
    level.load_ms = elapsed_ms(load_start);
    return level;
}

void print_stats(const LoadedLevel& level, double render_ms) {
    const bo2::platform::MemorySnapshot memory = bo2::platform::memory_snapshot();
    std::cout << "stats: load " << level.load_ms << " ms (archive " << level.archive_ms
              << ", parse " << level.parse_ms << ", textures " << level.textures_ms
              << ", lightmaps " << level.lightmaps_ms << ")";
    if (render_ms >= 0.0) {
        std::cout << ", render " << render_ms << " ms";
    }
    std::cout << ", rss " << memory.resident_bytes / (1024 * 1024)
              << " MB, peak " << memory.peak_resident_bytes / (1024 * 1024) << " MB\n";
}

void print_scene(const LoadedLevel& level) {
    const bo2::engine::LevelGeometry& geometry = level.geometry;
    std::cout << "cellgrup: " << level.name << " (models " << geometry.model_count
              << ", skipped " << geometry.skipped_models << ", triangles "
              << geometry.triangles.size() << ", vertices " << geometry.positions.size()
              << ", textures " << level.loaded_textures << "/" << geometry.texture_names.size()
              << ", lightmaps " << level.loaded_lightmaps << "/" << geometry.texture_names.size()
              << " (entries " << level.lightmap_entries << "))\n";
    if (geometry.has_cell_grid) {
        std::size_t portals = 0;
        std::size_t terrains = 0;
        for (const bo2::engine::LevelCell& cell : geometry.cell_grid.cells) {
            portals += cell.portals.size();
            terrains += cell.terrains.size();
        }
        std::cout << "cells: " << geometry.cell_grid.cells.size() << " (portals " << portals
                  << ", terrains " << terrains << ")";
        if (!geometry.cell_grid.cells.empty()) {
            std::cout << " [";
            for (std::size_t index = 0; index < geometry.cell_grid.cells.size(); ++index) {
                if (index != 0) {
                    std::cout << ", ";
                }
                std::cout << geometry.cell_grid.cells[index].name;
            }
            std::cout << "]";
        }
        std::cout << "\n";
        const bo2::engine::CellNavigation navigation(geometry.cell_grid);
        std::cout << "nav: " << geometry.cell_grid.cells.size() << " cells, "
                  << navigation.connections() << " links\n";
    }
    if (geometry.has_cell_bsp) {
        std::cout << "cellbsp: " << geometry.cell_bsp.nodes.size() << " nodes ("
                  << geometry.cell_bsp.leaf_count << " leaves, depth "
                  << geometry.cell_bsp.depth << ")\n";
    }
    const bo2::engine::CollisionWorld collision_world(geometry);
    std::cout << "collision mesh: " << collision_world.triangles() << " triangles\n";
    if (!geometry.collisions.empty()) {
        std::size_t bytes = 0;
        for (const bo2::engine::LevelCollision& collision : geometry.collisions) {
            bytes += collision.data.size();
        }
        std::cout << "collision: " << geometry.collisions.size() << " models (" << bytes
                  << " bytes)\n";
    }
    if (!geometry.skeletons.empty()) {
        std::size_t bones = 0;
        std::size_t weights = 0;
        for (const bo2::engine::ModelSkeleton& skeleton : geometry.skeletons) {
            bones += skeleton.bones.size();
            weights += skeleton.weights.size();
        }
        std::cout << "skeletons: " << geometry.skeletons.size() << " models (" << bones
                  << " bones, " << weights << " weights)\n";
    }
    if (geometry.has_instances) {
        std::cout << "instances: " << geometry.instances.size() << " (placed "
                  << geometry.placed_instances << ", skipped " << geometry.skipped_instances
                  << ")\n";
    }
    if (geometry.has_lights) {
        std::cout << "lights: " << geometry.lights.size() << "\n";
    }
    if (geometry.has_markers) {
        std::cout << "markers: " << geometry.markers.size() << "\n";
    }
    if (geometry.has_level_attributes) {
        std::cout << "attrs: " << geometry.level_attributes.size() << " tables\n";
    }
    if (!geometry.model_lightmaps.empty()) {
        std::size_t lit_triangles = 0;
        for (const bo2::engine::LevelTriangle& triangle : geometry.triangles) {
            if (triangle.lightmap >= 0) {
                ++lit_triangles;
            }
        }
        std::cout << "model lightmaps: " << geometry.model_lightmaps.size() << " (triangles "
                  << lit_triangles << ")\n";
    }
}

std::uint32_t cell_at(const bo2::engine::LevelGeometry& geometry,
                      const bo2::engine::Vec3& position) {
    for (std::size_t index = 0; index < geometry.cell_grid.cells.size(); ++index) {
        const bo2::engine::LevelCell& cell = geometry.cell_grid.cells[index];
        if (position.x >= cell.minimum.x && position.x <= cell.maximum.x &&
            position.y >= cell.minimum.y && position.y <= cell.maximum.y &&
            position.z >= cell.minimum.z && position.z <= cell.maximum.z) {
            return static_cast<std::uint32_t>(index);
        }
    }
    return 0;
}

void print_visibility(const LoadedLevel& level, const bo2::engine::Vec3& eye,
                      const bo2::engine::Vec3& target, float fov_degrees,
                      std::uint32_t width, std::uint32_t height) {
    const bo2::engine::LevelGeometry& geometry = level.geometry;
    if (geometry.markers.empty() && geometry.lights.empty()) {
        return;
    }
    bo2::engine::EntityWorld world;
    for (const bo2::engine::LevelMarker& marker : geometry.markers) {
        world.add(marker.position, 0.5f, cell_at(geometry, marker.position));
    }
    for (const bo2::engine::LevelLight& light : geometry.lights) {
        world.add(light.position, 0.5f, cell_at(geometry, light.position));
    }
    std::vector<bo2::engine::CellBounds> bounds;
    for (std::size_t index = 0; index < geometry.cell_grid.cells.size(); ++index) {
        const bo2::engine::LevelCell& cell = geometry.cell_grid.cells[index];
        bounds.push_back({static_cast<std::uint32_t>(index), cell.minimum, cell.maximum});
    }
    const bo2::engine::Vec3 extent = geometry.maximum - geometry.minimum;
    const float size = std::max({extent.x, extent.y, extent.z});
    bo2::engine::CellStreamer streamer(bounds);
    streamer.update(eye, std::max(size * 2.0f, 1.0f));
    bo2::engine::Mat4 view = bo2::engine::Mat4::look_at(eye, target, {0.0f, 0.0f, 1.0f});
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    bo2::engine::Mat4 projection = bo2::engine::Mat4::perspective(
        fov_degrees * 3.14159265358979323846f / 180.0f, aspect, 0.1f, 10000.0f);
    const bo2::engine::Frustum frustum =
        bo2::engine::frustum_from_view_projection(projection * view);
    std::size_t visible = 0;
    for (const bo2::engine::Entity& entity : world.entities()) {
        if (streamer.is_active(entity.cell) &&
            frustum.intersects_sphere(entity.position, entity.radius)) {
            ++visible;
        }
    }
    std::cout << "entities: " << world.entities().size() << " (visible " << visible
              << ", active cells " << streamer.active().size() << "/" << bounds.size() << ")\n";
}

void default_camera(const LoadedLevel& level, bo2::engine::Vec3& eye,
                    bo2::engine::Vec3& target) {
    const bo2::engine::Vec3 center = (level.geometry.minimum + level.geometry.maximum) * 0.5f;
    const bo2::engine::Vec3 extent = level.geometry.maximum - level.geometry.minimum;
    const float size = std::max({extent.x, extent.y, extent.z});
    eye = center + bo2::engine::Vec3{size * 0.9f, -size * 1.15f, size * 0.8f};
    target = center;
}

int render_level_frame(const Options& options) {
    if (options.out.empty()) {
        usage_error("--level requires --out");
    }
    const LoadedLevel level = load_level(options);
    bo2::platform::headless::FrameOptions frame = options.frame_options;
    if (!options.eye_set || !options.target_set) {
        default_camera(level, frame.camera.eye, frame.camera.target);
    }
    frame.camera.up = {0.0f, 0.0f, 1.0f};
    print_visibility(level, frame.camera.eye, frame.camera.target, frame.camera.fov_degrees,
                     frame.width, frame.height);
    const std::uint64_t render_start = bo2::platform::monotonic_nanoseconds();
    const std::vector<std::uint8_t> pixels =
        bo2::platform::headless::render_level(level.geometry, level.textures, frame);
    const double render_ms = elapsed_ms(render_start);
    bo2::platform::headless::write_ppm(options.out, frame.width, frame.height, pixels);
    print_scene(level);
    if (options.stats) {
        print_stats(level, render_ms);
    }
    std::cout << "frame: " << options.out << " (" << frame.width << 'x' << frame.height << ")\n";
    return 0;
}

int run_sdl(const Options& options) {
#ifdef BO2NX_HAVE_SDL
    bo2::platform::sdl::WindowOptions window;
    window.width = options.frame_options.width;
    window.height = options.frame_options.height;
    window.screenshot = options.out;
    window.camera.eye = options.frame_options.camera.eye;
    window.camera.target = options.frame_options.camera.target;
    window.camera.fov_degrees = options.frame_options.camera.fov_degrees;
    window.camera.up = {0.0f, 0.0f, 1.0f};
    window.material_mode = options.frame_options.material_mode;
    window.lightmap = options.frame_options.lightmap;
    window.stats = options.stats;
    if (options.big.empty()) {
        return bo2::platform::sdl::run_triangle(window);
    }
    const LoadedLevel level = load_level(options);
    if (!options.eye_set || !options.target_set) {
        default_camera(level, window.camera.eye, window.camera.target);
    }
    print_scene(level);
    print_visibility(level, window.camera.eye, window.camera.target,
                     window.camera.fov_degrees, window.width, window.height);
    if (options.stats) {
        print_stats(level, -1.0);
    }
    return bo2::platform::sdl::run_level(level.geometry, level.textures, window);
#else
    (void)options;
    throw std::runtime_error("this build has no SDL2/OpenGL backend");
#endif
}

int run_audio(const Options& options) {
#ifdef BO2NX_HAVE_SDL
    std::shared_ptr<const bo2::engine::PcmClip> event;
    std::shared_ptr<const bo2::engine::PcmClip> music;
    if (!options.event_ogg.empty()) {
        event = bo2::engine::decode_ogg(bo2::platform::FileSystem::read_binary(options.event_ogg));
    }
    if (!options.music_ogg.empty()) {
        music = bo2::engine::decode_ogg(bo2::platform::FileSystem::read_binary(options.music_ogg));
    }
    bo2::platform::sdl::AudioDevice device;
    if (music != nullptr) {
        device.start_music(music);
    }
    if (event != nullptr) {
        device.play_event(event);
    }
    std::cout << "audio: event " << (event ? event->frames() : 0) << " frames, music "
              << (music ? music->frames() : 0) << " frames, " << device.sample_rate() << " Hz\n";
    const std::uint64_t start = bo2::platform::monotonic_nanoseconds();
    const double limit = static_cast<double>(options.audio_seconds) * 1.0e9;
    while (static_cast<double>(bo2::platform::monotonic_nanoseconds() - start) < limit) {
        if (event != nullptr && music == nullptr && !device.events_active()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return 0;
#else
    (void)options;
    throw std::runtime_error("this build has no SDL2 audio backend");
#endif
}

int run_headless(const Options& options) {
    if (options.frames == 0) {
        usage_error("--headless requires --frames");
    }
    bo2::engine::FixedStep loop(1.0 / static_cast<double>(options.tick_rate));
    std::uint32_t completed = 0;
    while (completed < options.frames) {
        const std::uint32_t steps =
            loop.advance(1.0 / static_cast<double>(options.tick_rate), {});
        if (steps == 0) {
            break;
        }
        completed += steps;
    }
    std::cout << "headless: " << loop.ticks() << " ticks at " << options.tick_rate
              << " Hz (step " << loop.step_seconds() << " s)\n";
    return 0;
}

int print_erg(const std::string& path) {
    const std::string text = bo2::platform::FileSystem::read_text(path);
    const bo2::engine::ErgConfig config = bo2::engine::ErgConfig::parse(text);
    std::size_t entries = 0;
    for (const bo2::engine::ErgEntry& entry : config.entries()) {
        if (!entry.blank) {
            ++entries;
        }
    }
    std::cout << "erg: " << path << " (" << entries << " entries)\n";
    for (const bo2::engine::ErgEntry& entry : config.entries()) {
        if (!entry.blank) {
            std::cout << entry.key << '=' << entry.value << '\n';
        }
    }
    return 0;
}

int list_big(const std::string& path) {
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::load(path);
    std::cout << "entries: " << archive.entries().size() << '\n';
    for (const bo2::engine::BigEntry& entry : archive.entries()) {
        std::cout << entry.type << ' ' << entry.size << ' ' << entry.name << '\n';
    }
    return 0;
}

int render_frame(const Options& options) {
    if (options.big.empty()) {
        usage_error("--frame requires --big");
    }
    if (options.out.empty()) {
        usage_error("--frame requires --out");
    }
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::load(options.big);
    const bo2::engine::BigEntry* entry = pick_texture(archive, options.entry);
    if (entry == nullptr) {
        throw std::runtime_error("texture entry not found in " + options.big);
    }
    const std::vector<std::uint8_t> bytes = archive.extract(*entry);
    const bo2::engine::Image texture = bo2::engine::decode_dds(bytes);
    const std::vector<std::uint8_t> frame =
        bo2::platform::headless::render_textured_cube(texture, options.frame_options);
    bo2::platform::headless::write_ppm(options.out, options.frame_options.width,
                                       options.frame_options.height, frame);
    std::cout << "texture: " << entry->name << " (" << texture.width << 'x' << texture.height
              << ", " << entry->size << " bytes)\n";
    std::cout << "frame: " << options.out << " (" << options.frame_options.width << 'x'
              << options.frame_options.height << ")\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        std::cout << bo2::kProjectName << ' ' << bo2::version() << '\n';
        return 0;
    }
    try {
        const Options options = parse_options(argc, argv);
        if (!options.list_big.empty()) {
            return list_big(options.list_big);
        }
        if (!options.erg.empty()) {
            return print_erg(options.erg);
        }
        if (options.frame) {
            return render_frame(options);
        }
        if (options.level) {
            return render_level_frame(options);
        }
        if (options.headless) {
            return run_headless(options);
        }
        if (options.sdl) {
            return run_sdl(options);
        }
        if (!options.event_ogg.empty() || !options.music_ogg.empty()) {
            return run_audio(options);
        }
        std::cout << bo2::kProjectName << ' ' << bo2::version() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "bo2nx: " << error.what() << '\n';
        return 1;
    }
}
