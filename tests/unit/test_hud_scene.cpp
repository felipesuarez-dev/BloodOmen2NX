#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/cell_group.hpp"
#include "bo2/game/hud_scene.hpp"
#include "bo2/game/kain_tuning.hpp"
#include "bo2/game/kain_vitals.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

bool near(float a, float b, float tolerance = 1.0e-4f) {
    return std::fabs(a - b) <= tolerance;
}

bool near(const bo2::engine::Vec3& a, const bo2::engine::Vec3& b, float tolerance = 1.0e-3f) {
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

// A clip that scales bone 0 on one axis (Y by default) from 1 to `top` over
// `frames` frames.
bo2::engine::AnimReel make_fill_clip(std::uint32_t frames, float top,
                                     std::uint8_t component = 1) {
    bo2::engine::AnimReel reel;
    reel.fps = 30.0f;
    reel.frames = frames;
    reel.duration = static_cast<float>(frames - 1) / 30.0f;
    bo2::engine::AnimChannel channel;
    channel.node = 0;
    channel.type = 2;
    channel.component = component;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        channel.samples.push_back(1.0f + (top - 1.0f) * static_cast<float>(frame) /
                                             static_cast<float>(frames - 1));
    }
    reel.channels.push_back(channel);
    return reel;
}

void add_marker(bo2::engine::LevelGeometry& scene, const std::string& name,
                const std::string& entity_class, std::int32_t clip,
                const bo2::engine::Vec3& position) {
    bo2::engine::LevelMarker marker;
    marker.model = static_cast<std::int32_t>(scene.markers.size());
    marker.clip = clip;
    marker.name = name;
    marker.entity_class = entity_class;
    marker.position = position;
    scene.markers.push_back(marker);
}

// The objects of `ui/hud` the rules use, at their positions in the game data.
bo2::engine::LevelGeometry make_scene() {
    bo2::engine::LevelGeometry scene;
    add_marker(scene, "ui_hud_health_glass", "", -1, {-285.0f, 141.0f, -460.0f});
    add_marker(scene, "ui_hud_health_vial", "", -1, {-285.0f, -138.0f, -450.0f});
    add_marker(scene, "ui_hud_lore_glass", "KainModeHUDComponent", -1, {-261.0f, 127.0f, -460.0f});
    add_marker(scene, "ui_hud_lore_bottom", "KainModeHUDComponent", -1,
               {-261.0f, -138.0f, -450.0f});
    add_marker(scene, "_glass", "KainModeHUDComponent", -1, {-272.0f, 196.0f, -360.0f});
    add_marker(scene, "ui_hud_health_bar", "HUDHealthBar", 0, {-285.0f, -138.0f, -470.0f});
    add_marker(scene, "ui_hud_lore_bar", "HUDLoreBar", 1, {-261.0f, -138.0f, -470.0f});
    add_marker(scene, "_main_enemy", "EnemyModeHUDComponent", -1, {246.87f, 176.0f, -440.0f});
    add_marker(scene, "ui_hud_weapon_dagger", "HUDWeaponSubIcon", -1,
               {-154.49f, 180.95f, -387.62f});
    scene.cell_animations.push_back(make_fill_clip(28, 140.0f));
    add_marker(scene, "ui_hud_rage_bar", "HUDRageBar", 2, {-236.0f, 217.65f, -475.0f});
    scene.cell_animations.push_back(make_fill_clip(28, 133.0f));
    // The rage bar's clip scales X from 1 to 28 over 101 frames.
    scene.cell_animations.push_back(make_fill_clip(101, 28.0f, 0));
    return scene;
}

bo2::game::KainTuning make_tuning() {
    bo2::game::KainTuning tuning;
    tuning.lore_levels = {{6.0f, 30.0f}, {30.0f, 60.0f}, {950.0f, 500.0f}};
    tuning.max_rage = 100.0f;
    tuning.fury = {30.0f, {1.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.0f, 0.0f, 0.75f}};
    tuning.rupture = {40.0f, {0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 0.5f, 0.0f, 1.0f}};
    tuning.speed = {50.0f, {0.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.5f, 1.0f}};
    tuning.non_rage_gift_bar = {0.5f, 0.5f, 0.5f, 1.0f};
    return tuning;
}

const bo2::game::HudItem* find_item(const std::vector<bo2::game::HudItem>& items,
                                    std::uint32_t model) {
    for (const bo2::game::HudItem& item : items) {
        if (item.model == model) {
            return &item;
        }
    }
    return nullptr;
}

void test_mode_decides_what_is_visible() {
    const bo2::game::HudScene hud(make_scene());
    const bo2::game::KainTuning tuning = make_tuning();
    bo2::game::KainVitals vitals;
    bo2::game::set_lore_level(vitals, tuning, 0, 0.0f);
    bo2::game::reset_gifts(vitals, tuning);
    // FUN_00540b50: nothing outside the Kain modes or while paused.
    CHECK(hud.items(vitals, tuning, bo2::game::HudMode::Hidden, false).empty());
    CHECK(hud.items(vitals, tuning, bo2::game::HudMode::Kain, true).empty());
    const std::vector<bo2::game::HudItem> items =
        hud.items(vitals, tuning, bo2::game::HudMode::Kain, false);
    // The frame glass, three pieces for each of the two bars and the rage bar.
    CHECK(items.size() == 8);
    for (std::size_t index = 1; index < items.size(); ++index) {
        CHECK(items[index - 1].model < items[index].model);
    }
    // The enemy mode object and the weapon icon are not drawn.
    CHECK(find_item(items, 7) == nullptr);
    CHECK(find_item(items, 8) == nullptr);
    const bo2::game::HudItem* glass = find_item(items, 4);
    CHECK(glass != nullptr);
    if (glass != nullptr) {
        CHECK(near(glass->position, {-272.0f, 196.0f, -360.0f}));
        CHECK(near(glass->scale, {1.0f, 1.0f, 1.0f}));
    }
    CHECK(hud.items(vitals, tuning, bo2::game::HudMode::KainAndEnemy, false).size() == 8);
}

void test_health_bar_rule() {
    const bo2::game::HudScene hud(make_scene());
    const bo2::game::KainTuning tuning = make_tuning();
    bo2::game::KainVitals vitals;
    bo2::game::set_lore_level(vitals, tuning, 0, 0.0f);
    bo2::game::reset_gifts(vitals, tuning);
    // Level 0: 30 hit points of the 500 of the last level.
    const float length = 30.0f / 500.0f;
    std::vector<bo2::game::HudItem> items =
        hud.items(vitals, tuning, bo2::game::HudMode::Kain, false);
    const bo2::game::HudItem* bar = find_item(items, 5);
    const bo2::game::HudItem* glass = find_item(items, 0);
    const bo2::game::HudItem* vial = find_item(items, 1);
    CHECK(bar != nullptr && glass != nullptr && vial != nullptr);
    if (bar != nullptr && glass != nullptr && vial != nullptr) {
        // Full health: the clip at its end (140), times the length.
        CHECK(near(bar->scale, {1.0f, 140.0f * length, 1.0f}));
        // The bar and the vial sit between their own place and the glass's.
        CHECK(near(bar->position, {-285.0f, -138.0f * length + 141.0f * (1.0f - length),
                                   -470.0f * length - 460.0f * (1.0f - length)}));
        CHECK(near(vial->position, {-285.0f, -138.0f * length + 141.0f * (1.0f - length),
                                    -450.0f * length - 460.0f * (1.0f - length)}));
        CHECK(near(vial->scale, {1.0f, 1.0f, 1.0f}));
        CHECK(near(glass->position, {-285.0f, 141.0f, -460.0f}));
        CHECK(near(glass->scale, {1.0f, length, 1.0f}));
        // Dimmed while the health is at or above 0.3 of its maximum.
        CHECK(near(bar->colour, {0.5f, 0.5f, 0.5f}));
        CHECK(near(glass->colour, {1.0f, 1.0f, 1.0f}));
    }
    // One fifth of the health: the clip at a fifth of its length.
    vitals.health = 6.0f;
    items = hud.items(vitals, tuning, bo2::game::HudMode::Kain, false);
    bar = find_item(items, 5);
    CHECK(bar != nullptr);
    if (bar != nullptr) {
        CHECK(near(bar->scale.y, (1.0f + 139.0f * 0.2f) * length, 1.0e-3f));
        CHECK(near(bar->colour, {1.0f, 1.0f, 1.0f}));
    }
    vitals.health = 9.0f;  // exactly 0.3
    items = hud.items(vitals, tuning, bo2::game::HudMode::Kain, false);
    bar = find_item(items, 5);
    CHECK(bar != nullptr && near(bar->colour, {0.5f, 0.5f, 0.5f}));
    // A higher lore level makes the vial longer.
    bo2::game::set_lore_level(vitals, tuning, 2, 0.0f);
    items = hud.items(vitals, tuning, bo2::game::HudMode::Kain, false);
    bar = find_item(items, 5);
    glass = find_item(items, 0);
    CHECK(bar != nullptr && glass != nullptr);
    if (bar != nullptr && glass != nullptr) {
        CHECK(near(glass->scale.y, 1.0f));
        CHECK(near(bar->scale.y, 140.0f));
        CHECK(near(bar->position, {-285.0f, -138.0f, -470.0f}));
    }
}

void test_lore_bar_rule() {
    const bo2::game::HudScene hud(make_scene());
    const bo2::game::KainTuning tuning = make_tuning();
    bo2::game::KainVitals vitals;
    bo2::game::set_lore_level(vitals, tuning, 1, 15.0f);  // 15 of 30 lore
    bo2::game::reset_gifts(vitals, tuning);
    const float length = 30.0f / 950.0f;
    const std::vector<bo2::game::HudItem> items =
        hud.items(vitals, tuning, bo2::game::HudMode::Kain, false);
    const bo2::game::HudItem* bar = find_item(items, 6);
    const bo2::game::HudItem* glass = find_item(items, 2);
    const bo2::game::HudItem* bottom = find_item(items, 3);
    CHECK(bar != nullptr && glass != nullptr && bottom != nullptr);
    if (bar != nullptr && glass != nullptr && bottom != nullptr) {
        CHECK(near(bar->scale.y, (1.0f + 132.0f * 0.5f) * length, 1.0e-3f));
        CHECK(near(bar->colour, {1.0f, 1.0f, 1.0f}));
        CHECK(near(glass->scale, {1.0f, length, 1.0f}));
        CHECK(near(glass->position, {-261.0f, 127.0f, -460.0f}));
        CHECK(near(bottom->position, {-261.0f, -138.0f * length + 127.0f * (1.0f - length),
                                      -450.0f * length - 460.0f * (1.0f - length)}));
        CHECK(near(bar->position, {-261.0f, -138.0f * length + 127.0f * (1.0f - length),
                                   -470.0f * length - 460.0f * (1.0f - length)}));
    }
}

template <typename Exception, typename Callback>
bool throws(Callback callback) {
    try {
        callback();
    } catch (const Exception&) {
        return true;
    }
    return false;
}

// FUN_0053d180 with FUN_00425f50 and FUN_00425fd0.
void test_rage_bar_rule() {
    const bo2::game::HudScene hud(make_scene());
    const bo2::game::KainTuning tuning = make_tuning();
    bo2::game::KainVitals vitals;
    bo2::game::set_lore_level(vitals, tuning, 0, 0.0f);
    bo2::game::reset_gifts(vitals, tuning);
    // The player starts with no rage and no gift selected (FUN_00435b60).
    CHECK(vitals.rage == 0.0f);
    CHECK(vitals.max_rage == 100.0f);
    CHECK(vitals.selected_gift == bo2::game::DarkGift::None);
    const auto bar = [&]() {
        const std::vector<bo2::game::HudItem> items =
            hud.items(vitals, tuning, bo2::game::HudMode::Kain, false);
        const bo2::game::HudItem* item = find_item(items, 9);
        CHECK(item != nullptr);
        return item != nullptr ? *item : bo2::game::HudItem{};
    };
    // No rage: the clip at its start, in place, with the colour of a gift
    // that spends no rage.
    bo2::game::HudItem item = bar();
    CHECK(near(item.position, {-236.0f, 217.65f, -475.0f}));
    CHECK(near(item.scale, {1.0f, 1.0f, 1.0f}));
    CHECK(near(item.colour, {0.5f, 0.5f, 0.5f}));
    CHECK(near(item.alpha, 1.0f));
    // Half the rage: half the clip, 1 + 27 / 2.
    vitals.rage = 50.0f;
    CHECK(near(bar().scale, {14.5f, 1.0f, 1.0f}));
    // Fury costs 30: its second colour below the cost, its first from it on.
    vitals.selected_gift = bo2::game::DarkGift::Fury;
    vitals.rage = 29.0f;
    CHECK(near(bo2::game::rage_threshold(vitals, tuning), 30.0f));
    item = bar();
    CHECK(near(item.colour, {0.5f, 0.0f, 0.0f}));
    CHECK(near(item.alpha, 0.75f));
    vitals.rage = 30.0f;
    item = bar();
    CHECK(near(item.colour, {1.0f, 0.0f, 0.0f}));
    CHECK(near(item.alpha, 1.0f));
    // Speed costs 50.
    vitals.selected_gift = bo2::game::DarkGift::Speed;
    vitals.rage = 49.0f;
    CHECK(near(bar().colour, {0.0f, 0.0f, 0.5f}));
    vitals.rage = 50.0f;
    CHECK(near(bar().colour, {0.0f, 0.0f, 1.0f}));
    // Rupture has colours but FUN_00425f50 does not ask for its cost: the
    // threshold is the maximum rage.
    vitals.selected_gift = bo2::game::DarkGift::Rupture;
    CHECK(near(bo2::game::rage_threshold(vitals, tuning), 100.0f));
    vitals.rage = 99.0f;
    CHECK(near(bar().colour, {0.0f, 0.5f, 0.0f}));
    vitals.rage = 100.0f;
    CHECK(near(bar().colour, {0.0f, 1.0f, 0.0f}));
    // A gift that spends no rage: one colour, full or not.
    vitals.selected_gift = bo2::game::DarkGift::Stealth;
    CHECK(near(bar().colour, {0.5f, 0.5f, 0.5f}));
    vitals.rage = 0.0f;
    CHECK(near(bar().colour, {0.5f, 0.5f, 0.5f}));
    // Without a maximum rage there is no fraction to show.
    vitals.max_rage = 0.0f;
    CHECK(throws<std::invalid_argument>(
        [&] { hud.items(vitals, tuning, bo2::game::HudMode::Kain, false); }));
}

void test_rejects_what_it_cannot_draw() {
    bo2::engine::LevelGeometry no_vial = make_scene();
    no_vial.markers[1].name = "other";
    CHECK(throws<std::invalid_argument>([&] { bo2::game::HudScene hud(no_vial); }));
    bo2::engine::LevelGeometry no_clip = make_scene();
    no_clip.markers[6].clip = -1;
    CHECK(throws<std::invalid_argument>([&] { bo2::game::HudScene hud(no_clip); }));

    const bo2::game::HudScene hud(make_scene());
    const bo2::game::KainTuning tuning = make_tuning();
    const bo2::game::KainVitals empty;
    CHECK(throws<std::invalid_argument>(
        [&] { hud.items(empty, tuning, bo2::game::HudMode::Kain, false); }));
    bo2::game::KainVitals vitals;
    bo2::game::set_lore_level(vitals, tuning, 0, 0.0f);
    bo2::game::reset_gifts(vitals, tuning);
    const bo2::game::KainTuning no_table;
    CHECK(throws<std::out_of_range>(
        [&] { hud.items(vitals, no_table, bo2::game::HudMode::Kain, false); }));
}

// The real scene and the real tuning: what the HUD shows when Kain enters
// the slums.
bool test_game_data() {
    const char* root = std::getenv("BO2NX_ASSETS_DIR");
    if (root == nullptr) {
        return false;
    }
    const std::filesystem::path base = std::filesystem::path(root) / "media/gogames/bo2";
    const std::filesystem::path code = base / "code.big";
    const std::filesystem::path kain = base / "resources/levels/kain/kain.big";
    if (!std::filesystem::exists(code) || !std::filesystem::exists(kain)) {
        return false;
    }
    const bo2::engine::BigArchive ui = bo2::engine::BigArchive::load(code.string());
    const bo2::engine::BigEntry* entry = nullptr;
    for (const bo2::engine::BigEntry& candidate : ui.entries()) {
        if (candidate.type == "cellgrup" && candidate.name == "ui/hud") {
            entry = &candidate;
            break;
        }
    }
    CHECK(entry != nullptr);
    if (entry == nullptr) {
        return true;
    }
    const bo2::engine::LevelGeometry scene =
        bo2::engine::parse_cell_group(ui.extract(*entry), entry->symbols, ui.names());
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::load(kain.string());
    const bo2::engine::BigEntry* tune = archive.find("tunedata", "kain");
    CHECK(tune != nullptr);
    if (tune == nullptr) {
        return true;
    }
    const bo2::game::KainTuning tuning = bo2::game::parse_kain_tuning(archive.extract(*tune));
    bo2::game::KainVitals vitals;
    bo2::game::set_lore_level(vitals, tuning, bo2::game::start_lore_level(tuning, "slums"), 0.0f);
    bo2::game::reset_gifts(vitals, tuning);
    const bo2::game::HudScene hud(scene);
    const std::vector<bo2::game::HudItem> items =
        hud.items(vitals, tuning, bo2::game::HudMode::Kain, false);
    // `_main`, `_glass`, three pieces per bar and the rage bar.
    CHECK(items.size() == 9);
    std::size_t named = 0;
    for (const bo2::game::HudItem& item : items) {
        const bo2::engine::LevelMarker& marker = scene.markers[item.model];
        if (marker.name == "ui_hud_health_bar") {
            ++named;
            // 30 of 500 hit points long, full: 140 x 0.06.
            CHECK(near(item.scale.y, 8.4f, 1.0e-3f));
            CHECK(near(item.position.y, 141.0f - 279.0f * 0.06f, 1.0e-2f));
            CHECK(near(item.colour, {0.5f, 0.5f, 0.5f}));
        } else if (marker.name == "ui_hud_lore_bar") {
            ++named;
            // No lore yet: the clip at its start, 6 of 950 lore long.
            CHECK(near(item.scale.y, 6.0f / 950.0f, 1.0e-5f));
        } else if (marker.name == "ui_hud_health_glass") {
            ++named;
            CHECK(near(item.scale.y, 0.06f));
        } else if (marker.name == "ui_hud_rage_bar") {
            ++named;
            // No rage and no gift selected: the clip at its start and the
            // `nonRageGiftBar` colour of the tuning.
            CHECK(near(item.position, {-236.0f, 217.65f, -475.0f}, 1.0e-2f));
            CHECK(near(item.scale, {1.0f, 1.0f, 1.0f}));
            CHECK(near(item.colour, {0.2f, 0.2f, 0.8f}));
            CHECK(near(item.alpha, 0.5f));
        } else if (marker.name == "_main") {
            ++named;
            CHECK(near(item.position, {-192.0f, 176.0f, -440.0f}));
        }
    }
    CHECK(named == 5);
    return true;
}

}  // namespace

int main() {
    test_mode_decides_what_is_visible();
    test_health_bar_rule();
    test_lore_bar_rule();
    test_rage_bar_rule();
    test_rejects_what_it_cannot_draw();
    try {
        if (!test_game_data()) {
            std::cout << "hud scene asset test skipped (BO2NX_ASSETS_DIR not set)\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "hud scene asset test: " << error.what() << '\n';
        ++failures;
    }
    if (failures != 0) {
        std::cerr << failures << " hud scene check(s) failed\n";
        return 1;
    }
    std::cout << "hud scene tests passed\n";
    return 0;
}
