#include "bo2/game/hud_scene.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace bo2::game {

namespace {

// FUN_0053a9b0 compares the health fraction with the double at 0x0086a090.
constexpr float kLowHealthFraction = 0.3f;

std::uint32_t find_marker(const engine::LevelGeometry& scene, const std::string& name) {
    for (std::size_t index = 0; index < scene.markers.size(); ++index) {
        if (scene.markers[index].name == name) {
            return static_cast<std::uint32_t>(index);
        }
    }
    throw std::invalid_argument("the HUD scene has no object named " + name);
}

const engine::AnimReel& marker_clip(const engine::LevelGeometry& scene, std::uint32_t marker) {
    const std::int32_t clip = scene.markers[marker].clip;
    if (clip < 0 || static_cast<std::size_t>(clip) >= scene.cell_animations.size()) {
        throw std::invalid_argument("the HUD object " + scene.markers[marker].name +
                                    " has no clip");
    }
    return scene.cell_animations[static_cast<std::size_t>(clip)];
}

// FUN_00775bb0: the clip is set to `length * fraction` and the model is
// evaluated again. The clips of the bars scale their only bone, so the result
// is a scale per axis; an axis without a channel keeps 1.
engine::Vec3 clip_scale(const engine::AnimReel& clip, float fraction) {
    engine::Vec3 scale{1.0f, 1.0f, 1.0f};
    if (clip.frames == 0) {
        return scale;
    }
    const float frame = fraction * static_cast<float>(clip.frames - 1);
    for (const engine::AnimChannel& channel : clip.channels) {
        if (channel.node != 0 || channel.type != 2) {
            continue;
        }
        const float value = channel.sample(frame);
        if (channel.component == 0) {
            scale.x = value;
        } else if (channel.component == 1) {
            scale.y = value;
        } else if (channel.component == 2) {
            scale.z = value;
        }
    }
    return scale;
}

engine::Vec3 mix(const engine::Vec3& a, const engine::Vec3& b, float weight) {
    return a * weight + b * (1.0f - weight);
}

}  // namespace

HudScene::HudScene(const engine::LevelGeometry& scene) {
    for (const engine::LevelMarker& marker : scene.markers) {
        positions_.push_back(marker.position);
    }
    health_.bar = find_marker(scene, "ui_hud_health_bar");
    // The two children the health bar creates (FUN_0053a2e0).
    health_.glass = find_marker(scene, "ui_hud_health_glass");
    health_.bottom = find_marker(scene, "ui_hud_health_vial");
    health_.clip = marker_clip(scene, health_.bar);
    lore_.bar = find_marker(scene, "ui_hud_lore_bar");
    // The two objects the HUD looks up for the lore bar (FUN_0052ef60).
    lore_.glass = find_marker(scene, "ui_hud_lore_glass");
    lore_.bottom = find_marker(scene, "ui_hud_lore_bottom");
    lore_.clip = marker_clip(scene, lore_.bar);
    rage_bar_ = find_marker(scene, "ui_hud_rage_bar");
    rage_clip_ = marker_clip(scene, rage_bar_);
    for (std::size_t index = 0; index < scene.markers.size(); ++index) {
        const std::uint32_t marker = static_cast<std::uint32_t>(index);
        if (scene.markers[index].entity_class == "KainModeHUDComponent" &&
            marker != lore_.glass && marker != lore_.bottom) {
            frame_.push_back(marker);
        }
    }
}

// The per-frame rule of a bar: FUN_0053a9b0 (health) and FUN_0053cab0 (lore).
// `length` is the current maximum over the highest one. The bar and its glass
// are scaled by it in Y (FUN_00693340); the bar and the bottom piece are
// placed between their own position and the glass's (FUN_006930c0).
void HudScene::add_bar(std::vector<HudItem>& items, const Bar& bar, float fraction, float length,
                       const engine::Vec3& colour) const {
    const engine::Vec3& glass_position = positions_[bar.glass];
    HudItem fill;
    fill.model = bar.bar;
    fill.position = mix(positions_[bar.bar], glass_position, length);
    fill.scale = clip_scale(bar.clip, fraction);
    fill.scale.y *= length;
    fill.colour = colour;
    items.push_back(fill);
    HudItem glass;
    glass.model = bar.glass;
    glass.position = glass_position;
    glass.scale.y = length;
    items.push_back(glass);
    HudItem bottom;
    bottom.model = bar.bottom;
    bottom.position = mix(positions_[bar.bottom], glass_position, length);
    items.push_back(bottom);
}

std::vector<HudItem> HudScene::items(const KainVitals& vitals, const KainTuning& tuning,
                                     HudMode mode, bool paused) const {
    std::vector<HudItem> items;
    // FUN_00540b50, the rule of every Kain mode component.
    if ((mode != HudMode::Kain && mode != HudMode::KainAndEnemy) || paused) {
        return items;
    }
    if (!(vitals.max_health > 0.0f) || !(vitals.max_lore > 0.0f) ||
        !(vitals.max_rage > 0.0f)) {
        throw std::invalid_argument("the HUD needs a positive maximum health, lore and rage");
    }
    for (const std::uint32_t marker : frame_) {
        HudItem item;
        item.model = marker;
        item.position = positions_[marker];
        items.push_back(item);
    }
    // FUN_0053a9b0: the bar is dimmed unless the health is low.
    const float health = vitals.health / vitals.max_health;
    const float dim = health >= kLowHealthFraction ? 0.5f : 1.0f;
    add_bar(items, health_, health, vitals.max_health / top_max_health(tuning),
            {dim, dim, dim});
    // FUN_0053cab0.
    add_bar(items, lore_, vitals.lore / vitals.max_lore, vitals.max_lore / top_max_lore(tuning),
            {1.0f, 1.0f, 1.0f});
    // FUN_0053d180: the clip of the rage bar follows the rage, and its colour
    // says whether the rage reaches the threshold of the selected gift.
    const float rage = vitals.rage / vitals.max_rage;
    const RageBarColours colours = rage_bar_colours(vitals, tuning);
    const std::array<float, 4>& colour =
        rage < rage_threshold(vitals, tuning) / vitals.max_rage ? colours.cant_use
                                                                 : colours.can_use;
    HudItem rage_bar;
    rage_bar.model = rage_bar_;
    rage_bar.position = positions_[rage_bar_];
    rage_bar.scale = clip_scale(rage_clip_, rage);
    rage_bar.colour = {colour[0], colour[1], colour[2]};
    rage_bar.alpha = colour[3];
    items.push_back(rage_bar);
    std::sort(items.begin(), items.end(),
              [](const HudItem& a, const HudItem& b) { return a.model < b.model; });
    return items;
}

}  // namespace bo2::game
