#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

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
        // The HUD scene of code.big: a cell group whose markers name the
        // model and the clip of every object.
        const std::filesystem::path code =
            std::filesystem::path(root) / "media/gogames/bo2/code.big";
        if (std::filesystem::exists(code)) {
            const bo2::engine::BigArchive ui = bo2::engine::BigArchive::load(code.string());
            const bo2::engine::BigEntry* hud = nullptr;
            for (const bo2::engine::BigEntry& entry : ui.entries()) {
                if (entry.type == "cellgrup" && entry.name == "ui/hud") {
                    hud = &entry;
                    break;
                }
            }
            CHECK(hud != nullptr);
            if (hud != nullptr) {
                const bo2::engine::LevelGeometry scene =
                    bo2::engine::parse_cell_group(ui.extract(*hud), hud->symbols, ui.names());
                CHECK(scene.markers.size() == 42);
                CHECK(scene.cell_animations.size() == 7);
                std::vector<int> named(scene.cell_animations.size(), 0);
                for (std::size_t index = 0; index < scene.markers.size(); ++index) {
                    const bo2::engine::LevelMarker& marker = scene.markers[index];
                    CHECK(marker.model == static_cast<std::int32_t>(index));
                    if (marker.clip >= 0 &&
                        static_cast<std::size_t>(marker.clip) < named.size()) {
                        ++named[static_cast<std::size_t>(marker.clip)];
                    } else {
                        CHECK(marker.clip == -1);
                    }
                    if (marker.name == "ui_hud_health_bar") {
                        CHECK(marker.clip == 0 && marker.entity_class == "HUDHealthBar");
                        CHECK(marker.position.x == -285.0f && marker.position.y == -138.0f &&
                              marker.position.z == -470.0f);
                    } else if (marker.name == "ui_hud_rage_bar") {
                        CHECK(marker.clip == 5 && marker.entity_class == "HUDRageBar");
                    } else if (marker.name == "ui_hud_health_vial") {
                        CHECK(marker.clip == -1 && marker.entity_class.empty());
                    }
                }
                for (const int count : named) {
                    CHECK(count == 1);  // every clip belongs to one object
                }
            }
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
