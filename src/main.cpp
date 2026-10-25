#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <iostream>
#include <string>
#include <vector>

#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/cell_group.hpp"
#include "bo2/engine/dds.hpp"
#include "bo2/platform/headless/software_renderer.hpp"
#include "bo2/version.hpp"

namespace {

struct Options {
    std::string list_big;
    bool frame = false;
    bool level = false;
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
                << "  bo2nx --frame --big <file.big> --out <frame.ppm> [options]\n\n"
                << "Frame options:\n"
                << "  --entry <substring>   texture entry (default: first texture)\n"
                << "  --width <n>           frame width (default 640)\n"
                << "  --height <n>          frame height (default 480)\n"
                << "  --eye x,y,z           camera position (default 2.4,1.6,2.4)\n"
                << "  --target x,y,z        camera target (default 0,0,0)\n"
                << "  --fov <degrees>       vertical field of view (default 55)\n";
            std::exit(0);
        }
        if (argument == "--list-big") {
            options.list_big = next_argument(index, argc, argv);
        } else if (argument == "--frame") {
            options.frame = true;
        } else if (argument == "--level") {
            options.level = true;
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
        } else {
            usage_error("unknown argument: " + argument);
        }
    }
    const int modes = (!options.list_big.empty() ? 1 : 0) + (options.frame ? 1 : 0) +
                      (options.level ? 1 : 0);
    if (modes > 1) {
        usage_error("--list-big, --frame and --level are mutually exclusive");
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

int render_level_frame(const Options& options) {
    if (options.big.empty()) {
        usage_error("--level requires --big");
    }
    if (options.out.empty()) {
        usage_error("--level requires --out");
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
    const std::vector<std::uint8_t> bytes = archive.extract(*entry);
    const bo2::engine::LevelGeometry geometry =
        bo2::engine::parse_cell_group(bytes, entry->symbols, archive.names());

    std::vector<bo2::engine::Image> textures;
    textures.reserve(geometry.texture_names.size());
    std::size_t loaded_textures = 0;
    for (const std::string& name : geometry.texture_names) {
        const bo2::engine::BigEntry* texture_entry = archive.find("texture", name);
        if (texture_entry == nullptr) {
            textures.emplace_back();
            continue;
        }
        try {
            textures.push_back(bo2::engine::decode_dds(archive.extract(*texture_entry)));
            ++loaded_textures;
        } catch (const std::exception&) {
            textures.emplace_back();
        }
    }

    bo2::platform::headless::FrameOptions frame = options.frame_options;
    if (!options.eye_set || !options.target_set) {
        const bo2::engine::Vec3 center =
            (geometry.minimum + geometry.maximum) * 0.5f;
        const bo2::engine::Vec3 extent = geometry.maximum - geometry.minimum;
        const float size = std::max({extent.x, extent.y, extent.z});
        frame.camera.eye = center + bo2::engine::Vec3{size * 0.9f, -size * 1.15f, size * 0.8f};
        frame.camera.target = center;
    }
    frame.camera.up = {0.0f, 0.0f, 1.0f};
    const std::vector<std::uint8_t> pixels =
        bo2::platform::headless::render_level(geometry, textures, frame);
    bo2::platform::headless::write_ppm(options.out, frame.width, frame.height, pixels);
    std::cout << "cellgrup: " << entry->name << " (models " << geometry.model_count
              << ", skipped " << geometry.skipped_models << ", triangles "
              << geometry.triangles.size() << ", vertices " << geometry.positions.size()
              << ", textures " << loaded_textures << "/" << geometry.texture_names.size()
              << ")\n";
    std::cout << "frame: " << options.out << " (" << frame.width << 'x' << frame.height << ")\n";
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
        if (options.frame) {
            return render_frame(options);
        }
        if (options.level) {
            return render_level_frame(options);
        }
        std::cout << bo2::kProjectName << ' ' << bo2::version() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "bo2nx: " << error.what() << '\n';
        return 1;
    }
}
