// bo2nx_check_data: runs the readers of the port over every entry of the game
// containers (docs/DATA_COVERAGE.md). A reader that rejects an entry is
// reported; this tool never stops on the first failure.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "bo2/engine/data_check.hpp"

namespace {

struct Options {
    std::filesystem::path root;
    std::filesystem::path failures;
};

int usage() {
    std::cerr << "usage: bo2nx_check_data <directory> [--failures <file>]\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return usage();
    }
    Options options;
    options.root = argv[1];
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--failures" && index + 1 < argc) {
            options.failures = argv[++index];
        } else {
            return usage();
        }
    }
    if (!std::filesystem::is_directory(options.root)) {
        std::cerr << "bo2nx_check_data: " << options.root.string() << " is not a directory\n";
        return 2;
    }

    std::vector<std::filesystem::path> containers;
    try {
        for (const std::filesystem::directory_entry& item :
             std::filesystem::recursive_directory_iterator(options.root)) {
            if (item.is_regular_file() && item.path().extension() == ".big") {
                containers.push_back(item.path());
            }
        }
    } catch (const std::filesystem::filesystem_error& error) {
        std::cerr << "bo2nx_check_data: " << error.what() << '\n';
        return 2;
    }
    std::sort(containers.begin(), containers.end());

    bo2::engine::DataReport report;
    bool load_failed = false;
    for (const std::filesystem::path& path : containers) {
        const std::string container = path.string();
        try {
            const bo2::engine::BigArchive archive = bo2::engine::BigArchive::load(container);
            bo2::engine::check_archive(archive, container, report);
        } catch (const std::exception& error) {
            std::cout << "load-fail " << container << ": " << error.what() << '\n';
            load_failed = true;
        }
    }

    std::cout << "containers " << report.containers << '\n';
    for (const auto& type : report.types) {
        std::cout << "type " << type.first << " entries " << type.second.entries << " checked "
                  << type.second.checked << " failed " << type.second.failed << '\n';
    }
    for (const auto& type : report.types) {
        std::size_t shown = 0;
        for (const bo2::engine::DataFailure& failure : report.failures) {
            if (failure.type != type.first) {
                continue;
            }
            if (shown == 20) {
                break;
            }
            std::cout << "fail " << failure.type << ' ' << failure.container << ':'
                      << failure.entry << ": " << failure.message << '\n';
            ++shown;
        }
    }

    if (!options.failures.empty()) {
        std::ofstream stream(options.failures);
        if (!stream) {
            std::cerr << "bo2nx_check_data: cannot write " << options.failures.string() << '\n';
            return 2;
        }
        for (const bo2::engine::DataFailure& failure : report.failures) {
            stream << failure.container << '\t' << failure.type << '\t' << failure.entry << '\t'
                   << failure.message << '\n';
        }
    }
    return load_failed ? 1 : 0;
}
