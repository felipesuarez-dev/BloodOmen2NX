#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <iostream>
#include <string>
#include <vector>

#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/cell_group.hpp"
#include "bo2/engine/dds.hpp"
#include "bo2/engine/erg.hpp"
#include "bo2/platform/headless/software_renderer.hpp"
#include "bo2/version.hpp"
#ifdef BO2NX_HAVE_SDL
#include "bo2/platform/sdl/sdl_backend.hpp"
#endif

namespace {

struct Options {
    std::string list_big;
    std::string erg;
    bool frame = false;
    bool level = false;
    bool sdl = false;
    bool eye_set = false;
    bool target_set = false;
    std::string big;
    std::string entry;
    std::string out;
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
                << "  bo2nx --sdl [--big <file.big>] [--out <screenshot.ppm>] [options]\n\n"
                << "Frame options:\n"
                << "  --entry <substring>   texture entry (default: first texture)\n"
                << "  --width <n>           frame width (default 640)\n"
                << "  --height <n>          frame height (default 480)\n"
                << "  --eye x,y,z           camera position (default 2.4,1.6,2.4)\n"
                << "  --target x,y,z        camera target (default 0,0,0)\n"
                << "  --fov <degrees>       vertical field of view (default 55)\n"
                << "  --material <mode>     material tint: off, diffuse, layers, layers2x\n"
                << "  --lightmap            sample the .slm light maps on textured surfaces\n";
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
        } else {
            usage_error("unknown argument: " + argument);
        }
    }
    const int modes = (!options.list_big.empty() ? 1 : 0) + (!options.erg.empty() ? 1 : 0) +
                      (options.frame ? 1 : 0) + (options.level ? 1 : 0) +
                      (options.sdl ? 1 : 0);
    if (modes > 1) {
        usage_error("--list-big, --erg, --frame, --level and --sdl are mutually exclusive");
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
};

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
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::load(options.big);
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
    level.geometry = bo2::engine::parse_cell_group(archive.extract(*entry), entry->symbols,
                                                   archive.names());
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
    return level;
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
    }
    if (geometry.has_cell_bsp) {
        std::cout << "cellbsp: " << geometry.cell_bsp.nodes.size() << " nodes ("
                  << geometry.cell_bsp.leaf_count << " leaves, depth "
                  << geometry.cell_bsp.depth << ")\n";
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
    const std::vector<std::uint8_t> pixels =
        bo2::platform::headless::render_level(level.geometry, level.textures, frame);
    bo2::platform::headless::write_ppm(options.out, frame.width, frame.height, pixels);
    print_scene(level);
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
    if (options.big.empty()) {
        return bo2::platform::sdl::run_triangle(window);
    }
    const LoadedLevel level = load_level(options);
    if (!options.eye_set || !options.target_set) {
        default_camera(level, window.camera.eye, window.camera.target);
    }
    print_scene(level);
    return bo2::platform::sdl::run_level(level.geometry, level.textures, window);
#else
    (void)options;
    throw std::runtime_error("this build has no SDL2/OpenGL backend");
#endif
}

int print_erg(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot read " + path);
    }
    const std::string text((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());
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
        if (options.sdl) {
            return run_sdl(options);
        }
        std::cout << bo2::kProjectName << ' ' << bo2::version() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "bo2nx: " << error.what() << '\n';
        return 1;
    }
}
