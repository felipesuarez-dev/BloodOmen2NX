#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
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

// The `texture` chunk of the HUD and of sl01: every record is read as it is
// and the whole list of each material is kept.
void test_texture_lists_of_the_game_data(const std::filesystem::path& root) {
    const std::filesystem::path code = root / "media/gogames/bo2/code.big";
    if (!std::filesystem::exists(code)) {
        return;
    }
    const bo2::engine::BigArchive ui = bo2::engine::BigArchive::load(code.string());
    const bo2::engine::BigEntry* hud = nullptr;
    for (const bo2::engine::BigEntry& entry : ui.entries()) {
        if (entry.type == "cellgrup" && entry.name == "ui/hud") {
            hud = &entry;
            break;
        }
    }
    CHECK(hud != nullptr);
    if (hud == nullptr) {
        return;
    }
    const bo2::engine::LevelGeometry scene =
        bo2::engine::parse_cell_group(ui.extract(*hud), hud->symbols, ui.names());
    std::size_t multiple = 0;
    std::size_t total = 0;
    const std::vector<std::int32_t>* icon = nullptr;
    for (const std::vector<std::int32_t>& list : scene.material_textures) {
        if (list.size() > 1) {
            ++multiple;
        }
        total += list.size();
        if (!list.empty() && list.front() >= 0 &&
            static_cast<std::size_t>(list.front()) < scene.texture_names.size() &&
            scene.texture_names[static_cast<std::size_t>(list.front())] == "ui/hud/icon_none") {
            icon = &list;
        }
    }
    CHECK(multiple == 7);
    CHECK(total == 646);
    CHECK(icon != nullptr);
    if (icon != nullptr) {
        const std::vector<std::string> expected = {
            "ui/hud/icon_none",    "ui/hud/icon_charm", "ui/hud/icon_jump",
            "ui/hud/icon_speed",   "ui/hud/icon_telekinesis", "ui/hud/icon_stealth",
            "ui/hud/icon_rupture", "ui/hud/icon_fury", "ui/hud/icon_slord"};
        CHECK(icon->size() == expected.size());
        for (std::size_t index = 0; index < expected.size() && index < icon->size(); ++index) {
            const std::int32_t texture = (*icon)[index];
            CHECK(texture >= 0);
            if (texture >= 0) {
                CHECK(scene.texture_names[static_cast<std::size_t>(texture)] == expected[index]);
            }
        }
    }

    const std::filesystem::path level =
        root / "media/gogames/bo2/resources/levels/slums/sl01.big";
    if (!std::filesystem::exists(level)) {
        return;
    }
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::load(level.string());
    const bo2::engine::BigEntry* cell = nullptr;
    for (const bo2::engine::BigEntry& entry : archive.entries()) {
        if (entry.type == "cellgrup" && entry.name == "slums/sl01") {
            cell = &entry;
            break;
        }
    }
    CHECK(cell != nullptr);
    if (cell == nullptr) {
        return;
    }
    const bo2::engine::LevelGeometry geometry =
        bo2::engine::parse_cell_group(archive.extract(*cell), cell->symbols, archive.names());
    multiple = 0;
    total = 0;
    for (const std::vector<std::int32_t>& list : geometry.material_textures) {
        if (list.size() > 1) {
            ++multiple;
        }
        total += list.size();
    }
    CHECK(multiple == 1);
    CHECK(total == 1112);
}

void check_hud_cull(const bo2::engine::LevelGeometry& scene, const std::string& name,
                    std::uint8_t expected) {
    const bo2::engine::LevelMarker* marker = nullptr;
    for (const bo2::engine::LevelMarker& candidate : scene.markers) {
        if (candidate.name == name) {
            marker = &candidate;
            break;
        }
    }
    CHECK(marker != nullptr);
    if (marker == nullptr) {
        return;
    }
    bool found = false;
    for (const bo2::engine::LevelTriangle& triangle : scene.triangles) {
        if (triangle.model != static_cast<std::uint32_t>(marker->model)) {
            continue;
        }
        found = true;
        CHECK(triangle.material < scene.material_cull.size());
        if (triangle.material < scene.material_cull.size()) {
            CHECK(scene.material_cull[triangle.material] == expected);
        }
    }
    CHECK(found);
}

// The six pieces that discard back faces and the pieces that keep both, read
// from word 1 of the mtlctrl records of ui/hud (0x4c00 and 0x8c00).
void test_hud_cull_flags(const bo2::engine::LevelGeometry& scene) {
    for (const char* name :
         {"ui_hud_darkgift_icon", "ui_hud_enemy_icon", "ui_hud_screen_cut",
          "ui_hud_letterbox", "ui_hud_anim_charm_haze", "ui_hud_pause_fade"}) {
        check_hud_cull(scene, name, 1);
    }
    for (const char* name :
         {"_main", "_glass", "ui_hud_health_bar", "ui_hud_lore_bar", "ui_hud_rage_bar",
          "ui_hud_health_glass", "ui_hud_lore_glass", "ui_hud_health_vial",
          "ui_hud_lore_bottom"}) {
        check_hud_cull(scene, name, 0);
    }
}

// The RenderPriority of each HUD model, read from its mdlattr table.
void check_priority(const bo2::engine::LevelGeometry& scene, const std::string& name,
                    float expected) {
    const bo2::engine::LevelMarker* marker = nullptr;
    for (const bo2::engine::LevelMarker& candidate : scene.markers) {
        if (candidate.name == name) {
            marker = &candidate;
            break;
        }
    }
    CHECK(marker != nullptr);
    if (marker == nullptr || marker->model < 0) {
        return;
    }
    const std::size_t model = static_cast<std::size_t>(marker->model);
    CHECK(model < scene.model_render_priority.size());
    if (model < scene.model_render_priority.size()) {
        CHECK(std::fabs(scene.model_render_priority[model] - expected) < 1.0e-5f);
    }
}

void test_hud_render_priorities(const bo2::engine::LevelGeometry& scene) {
    struct Entry {
        const char* name;
        float priority;
    };
    const Entry entries[] = {
        {"ui_hud_screen_cut", 8.0f},
        {"ui_hud_letterbox", 9.0f},
        {"ui_hud_pause_fade", 15.0f},
        {"ui_hud_anim_charm_haze", 20.0f},
        {"ui_hud_health_bar", 30.0f},
        {"ui_hud_lore_bar", 30.0f},
        {"ui_hud_rage_bar", 30.0f},
        {"ui_hud_health_glass", 40.0f},
        {"ui_hud_lore_glass", 40.0f},
        {"ui_hud_health_vial", 50.0f},
        {"ui_hud_lore_bottom", 50.0f},
        {"_main", 60.0f},
        {"ui_hud_darkgift_icon", 70.0f},
        {"_glass", 80.0f},
        {"ui_hud_intent_glow", 90.0f},
        {"ui_hud_hilit_lore_bar", 100.0f},
        {"ui_hud_hilit_health_bar", 100.0f},
        {"ui_hud_hilit_dark_gift_icon", 100.0f},
        {"ui_hud_hilit_rage_bar", 100.0f},
    };
    for (const Entry& entry : entries) {
        check_priority(scene, entry.name, entry.priority);
    }
}

// The 16 cell groups whose `mdlattr` tables carry array records: the walk of
// the table consumes each one exactly (docs/FORMATS/models.md, "mdlattr").
void test_cell_groups_with_attribute_arrays(const std::filesystem::path& root) {
    struct Case {
        const char* container;
        const char* entry;
    };
    const Case cases[] = {
        {"media/gogames/bo2/resources/levels/canyons/cy01.big", "canyons/cy01"},
        {"media/gogames/bo2/resources/levels/canyons/cy04.big", "canyons/cy04"},
        {"media/gogames/bo2/resources/levels/canyons/cy04b.big", "canyons/cy04b"},
        {"media/gogames/bo2/resources/levels/canyons/cy05.big", "canyons/cy05"},
        {"media/gogames/bo2/resources/levels/canyons/cy06.big", "canyons/cy06"},
        {"media/gogames/bo2/resources/levels/canyons/cy07.big", "canyons/cy07"},
        {"media/gogames/bo2/resources/levels/slums/sl07.big", "slums/sl07"},
        {"media/gogames/bo2/resources/levels/smugglersden/RegionFiles/sd03.big",
         "smugglersden/regionfiles/sd03"},
        {"media/gogames/bo2/resources/levels/smugglersden/RegionFiles/sd05.big",
         "smugglersden/regionfiles/sd05"},
        {"media/gogames/bo2/resources/levels/uppercitynew/uc02a.big", "uppercitynew/uc02a"},
        {"media/gogames/bo2/resources/levels/uppercitynew/uc02a2.big", "uppercitynew/uc02a"},
        {"media/gogames/bo2/resources/levels/wharves/regionfiles/wh01.big",
         "wharves/regionfiles/wh01"},
        {"media/gogames/bo2/resources/levels/wharves/regionfiles/wh05.big",
         "wharves/regionfiles/wh05"},
        {"media/gogames/bo2/resources/levels/wharves/regionfiles/wh11.big",
         "wharves/regionfiles/wh11"},
        {"media/gogames/bo2/resources/levels/wharves/regionfiles/wh12.big",
         "wharves/regionfiles/wh12"},
        {"media/gogames/bo2/resources/levels/wharves/regionfiles/wh13.big",
         "wharves/regionfiles/wh13"},
    };
    for (const Case& item : cases) {
        const bo2::engine::BigArchive archive =
            bo2::engine::BigArchive::load((root / item.container).string());
        const bo2::engine::BigEntry* entry = archive.find("cellgrup", item.entry);
        CHECK(entry != nullptr);
        if (entry != nullptr) {
            const bo2::engine::LevelGeometry scene = bo2::engine::parse_cell_group(
                archive.extract(*entry), entry->symbols, archive.names());
            CHECK(!scene.model_render_priority.empty());
        }
    }
}

// The attribute tables of the HUD and of sl01: the records of every model
// and the names of the control of BO2NX-115 (docs/FORMATS/models.md,
// "mdlattr").
void test_model_attribute_counts(const std::filesystem::path& root) {
    const std::filesystem::path code = root / "media/gogames/bo2/code.big";
    if (!std::filesystem::exists(code)) {
        return;
    }
    const bo2::engine::BigArchive ui = bo2::engine::BigArchive::load(code.string());
    const bo2::engine::BigEntry* hud = ui.find("cellgrup", "ui/hud");
    CHECK(hud != nullptr);
    if (hud != nullptr) {
        const bo2::engine::LevelGeometry scene =
            bo2::engine::parse_cell_group(ui.extract(*hud), hud->symbols, ui.names());
        CHECK(scene.model_count == 42);
        CHECK(scene.model_attributes.size() == scene.model_ranges.size());
        std::map<std::string, std::size_t> counts;
        std::size_t records = 0;
        for (const std::vector<bo2::engine::ModelAttribute>& table : scene.model_attributes) {
            records += table.size();
            for (const bo2::engine::ModelAttribute& attribute : table) {
                ++counts[attribute.name];
            }
        }
        CHECK(records == 206);
        CHECK(counts.size() == 5);
        CHECK(counts["ObjectType"] == 42);
        CHECK(counts["UseMaxMesh"] == 42);
        CHECK(counts["RenderPriority"] == 42);
        CHECK(counts["NoCollisionModel"] == 42);
        CHECK(counts["EntityClass"] == 38);
    }

    const std::filesystem::path level =
        root / "media/gogames/bo2/resources/levels/slums/sl01.big";
    if (!std::filesystem::exists(level)) {
        return;
    }
    const bo2::engine::BigArchive archive =
        bo2::engine::BigArchive::load(level.string());
    const bo2::engine::BigEntry* level_entry = archive.find("cellgrup", "slums/sl01");
    CHECK(level_entry != nullptr);
    if (level_entry == nullptr) {
        return;
    }
    const bo2::engine::LevelGeometry scene =
        bo2::engine::parse_cell_group(archive.extract(*level_entry), level_entry->symbols,
                                      archive.names());
    CHECK(scene.model_count == 608);
    CHECK(scene.model_attributes.size() == scene.model_ranges.size());
    std::map<std::string, std::size_t> counts;
    std::size_t records = 0;
    for (const std::vector<bo2::engine::ModelAttribute>& table : scene.model_attributes) {
        records += table.size();
        for (const bo2::engine::ModelAttribute& attribute : table) {
            ++counts[attribute.name];
        }
    }
    CHECK(records == 479);
    CHECK(counts.size() == 18);
    CHECK(counts["ObjectType"] == 212);
    CHECK(counts["NoCollisionModel"] == 168);
    CHECK(counts["VertexLighting"] == 50);
    CHECK(counts["EntityClass"] == 9);
    CHECK(counts["UseMaxMesh"] == 9);
    CHECK(counts["NoEdgeGrab"] == 5);
    CHECK(counts["Animated"] == 4);
    CHECK(counts["NoCameraCollide"] == 4);
    CHECK(counts["TransparentToLight"] == 4);
    CHECK(counts["VertexAlpha"] == 2);
    CHECK(counts["DFacade"] == 2);
    CHECK(counts["Lightmaps"] == 2);
    CHECK(counts["SkySphere"] == 2);
    CHECK(counts["SortBias"] == 2);
    CHECK(counts["TransparentToLineOfSight"] == 1);
    CHECK(counts["NoLighting"] == 1);
    CHECK(counts["SmoothLighting"] == 1);
    CHECK(counts["Slide"] == 1);
    std::size_t sphere = 0;
    for (std::uint32_t model = 0; model < scene.model_attributes.size(); ++model) {
        if (bo2::engine::attribute_set(scene, model, "SkySphere")) {
            ++sphere;
        }
    }
    CHECK(sphere == 2);
}

// The load numbers of every cell group the reader accepts, pinned in
// tests/data/cell_group_baseline.tsv (docs/DATA_COVERAGE.md, "Cell group
// baseline"). The container paths of the file are relative to the directory
// bo2nx_check_data reads, so the test prefixes media/gogames/bo2. Reading the
// 206 groups takes the test over its 60-second budget, so it checks the
// slums levels and code.big and leaves the rest of the file to the tool.
void test_cell_group_baseline(const std::filesystem::path& root) {
    const std::filesystem::path source =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    const std::filesystem::path file = source / "tests/data/cell_group_baseline.tsv";
    std::ifstream stream(file);
    CHECK(stream.good());
    if (!stream) {
        return;
    }
    const std::filesystem::path game = root / "media/gogames/bo2";
    std::string line;
    std::string current;
    std::optional<bo2::engine::BigArchive> archive;
    std::size_t rows = 0;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        std::vector<std::string> fields;
        std::size_t start = 0;
        while (true) {
            const std::size_t tab = line.find('\t', start);
            fields.push_back(line.substr(start, tab - start));
            if (tab == std::string::npos) {
                break;
            }
            start = tab + 1;
        }
        CHECK(fields.size() == 6);
        if (fields.size() != 6) {
            continue;
        }
        if (fields[0] != "code.big" && fields[0].rfind("resources/levels/slums/", 0) != 0) {
            continue;
        }
        if (fields[0] != current) {
            archive = bo2::engine::BigArchive::load((game / fields[0]).string());
            current = fields[0];
        }
        const bo2::engine::BigEntry* entry = archive->find("cellgrup", fields[1]);
        CHECK(entry != nullptr);
        if (entry == nullptr) {
            continue;
        }
        const bo2::engine::LevelGeometry geometry = bo2::engine::parse_cell_group(
            archive->extract(*entry), entry->symbols, archive->names());
        CHECK(geometry.model_count == std::stoul(fields[2]));
        CHECK(geometry.skipped_models == std::stoul(fields[3]));
        CHECK(geometry.triangles.size() == std::stoul(fields[4]));
        CHECK(geometry.positions.size() == std::stoul(fields[5]));
        ++rows;
    }
    // The four cell groups of code.big and the nine of the slums levels.
    CHECK(rows == 13);
}

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
                test_hud_cull_flags(scene);
                test_hud_render_priorities(scene);
            }
        }
        test_texture_lists_of_the_game_data(root);
        test_cell_groups_with_attribute_arrays(root);
        test_model_attribute_counts(root);
        test_cell_group_baseline(root);
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
