#pragma once

#include <cstdint>
#include <string_view>

#include "bo2/game/kain_tuning.hpp"

namespace bo2::game {

// Kain's lore and health counters. The original keeps them in two objects of
// the player: the lore object (FUN_00425fc0) and the health object
// (FUN_00425fb0). Each counter has the value the HUD shows and the target it
// grows to (FUN_004210b0 moves the shown value; that growth is not ported).
struct KainVitals {
    std::int32_t lore_level = 0;     // lore object +0x00, an index into the lore level table
    float lore = 0.0f;               // +0x04, shown
    float lore_target = 0.0f;        // +0x08
    float max_lore = 0.0f;           // +0x0C, shown
    float max_lore_target = 0.0f;    // +0x10
    float health = 0.0f;             // health object +0x0C, shown
    float health_target = 0.0f;      // +0x10
    float max_health = 0.0f;         // +0x14, shown
    float max_health_target = 0.0f;  // +0x18
};

// FUN_00420ef0 and the FUN_00420700 it ends with: enters lore level `level`
// with `lore` points. The maximum lore is `maxLore` of the level's record and
// the lore is clamped to it; the health and the maximum health become its
// `maxHitPoints`. Throws std::out_of_range when `level` is not a record of
// `tuning.lore_levels`. (The original reads the last record for the lore when
// `level` equals the record count; its health read has no such guard.)
void set_lore_level(KainVitals& vitals, const KainTuning& tuning, std::int32_t level, float lore);

// FUN_004256c0: the lore level of the `debugHealth` record named
// `level_name`, or `defaultHealth.loreLevel` when no record has that name.
// The comparison is exact.
std::int32_t start_lore_level(const KainTuning& tuning, std::string_view level_name);

// FUN_004208a0: `maxHitPoints` of the last lore level, the longest health
// vial. Throws std::out_of_range when the table is empty.
float top_max_health(const KainTuning& tuning);

// FUN_00421020: `maxLore` of the last lore level. Throws std::out_of_range
// when the table is empty.
float top_max_lore(const KainTuning& tuning);

}  // namespace bo2::game
