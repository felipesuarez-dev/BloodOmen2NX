#include "bo2/game/kain_vitals.hpp"

#include <stdexcept>

namespace bo2::game {

void set_lore_level(KainVitals& vitals, const KainTuning& tuning, std::int32_t level, float lore) {
    if (level < 0 || static_cast<std::size_t>(level) >= tuning.lore_levels.size()) {
        throw std::out_of_range("lore level is not in the lore level table");
    }
    const LoreLevel& record = tuning.lore_levels[static_cast<std::size_t>(level)];
    // FUN_00420ef0.
    vitals.lore_level = level;
    vitals.lore = lore;
    vitals.lore_target = lore;
    vitals.max_lore = record.max_lore;
    vitals.max_lore_target = record.max_lore;
    if (vitals.max_lore_target < lore) {
        vitals.lore_target = vitals.max_lore_target;
    }
    if (vitals.lore_target < lore) {
        vitals.lore = vitals.lore_target;
    }
    // FUN_00420700.
    vitals.max_health = record.max_hit_points;
    vitals.max_health_target = record.max_hit_points;
    vitals.health = record.max_hit_points;
    vitals.health_target = record.max_hit_points;
}

std::int32_t start_lore_level(const KainTuning& tuning, std::string_view level_name) {
    for (const StartLoreLevel& record : tuning.start_lore_levels) {
        if (record.level == level_name) {
            return record.lore_level;
        }
    }
    return tuning.default_lore_level;
}

float top_max_health(const KainTuning& tuning) {
    if (tuning.lore_levels.empty()) {
        throw std::out_of_range("the lore level table is empty");
    }
    return tuning.lore_levels.back().max_hit_points;
}

float top_max_lore(const KainTuning& tuning) {
    if (tuning.lore_levels.empty()) {
        throw std::out_of_range("the lore level table is empty");
    }
    return tuning.lore_levels.back().max_lore;
}

}  // namespace bo2::game
