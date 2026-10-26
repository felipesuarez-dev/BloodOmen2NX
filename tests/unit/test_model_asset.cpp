#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/cell_group.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

}  // namespace

int main() {
    const char* root = std::getenv("BO2NX_ASSETS_DIR");
    if (root == nullptr) {
        std::cout << "model asset test skipped (BO2NX_ASSETS_DIR is unset)\n";
        return 0;
    }
    const std::filesystem::path path =
        std::filesystem::path(root) /
        "media/gogames/bo2/resources/levels/sanctuary/sanctuary.big";
    if (!std::filesystem::exists(path)) {
        std::cout << "model asset test skipped (sanctuary.big not found)\n";
        return 0;
    }
    try {
        const bo2::engine::BigArchive archive = bo2::engine::BigArchive::load(path.string());
        const bo2::engine::BigEntry* model = nullptr;
        for (const bo2::engine::BigEntry& entry : archive.entries()) {
            if (entry.type == "model" && entry.name == "weapons/daggers/dagger/dagger") {
                model = &entry;
                break;
            }
        }
        CHECK(model != nullptr);
        if (model != nullptr) {
            const bo2::engine::LevelGeometry asset = bo2::engine::parse_model_asset(
                archive.extract(*model), model->symbols, archive.names());
            CHECK(!asset.positions.empty());
            CHECK(!asset.triangles.empty());
            CHECK(asset.model_ranges.size() == 1);
            CHECK(asset.model_ranges[0].present);
        }
    } catch (const std::exception& error) {
        std::cerr << "model asset test: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "model asset tests: ok\n";
    return 0;
}
