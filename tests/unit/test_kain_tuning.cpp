#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "bo2/common/reader.hpp"
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

constexpr std::size_t kBlobSize = 0x1000;

void put_float(std::vector<std::uint8_t>& data, std::size_t blob_offset, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    for (std::size_t index = 0; index < 4; ++index) {
        data[4 + blob_offset + index] = static_cast<std::uint8_t>((bits >> (8 * index)) & 0xFF);
    }
}

// The values the real kain tunedata holds, written at their B offsets.
std::vector<std::uint8_t> make_blob() {
    std::vector<std::uint8_t> data(4 + kBlobSize, 0);
    for (std::size_t index = 0; index < 4; ++index) {
        data[index] = static_cast<std::uint8_t>((kBlobSize >> (8 * index)) & 0xFF);
    }
    put_float(data, 0x00C, 0.1f);
    const float jump[9] = {-2500.0f, -700.0f, -128.0f, 512.0f, 160.0f,
                           64.0f,    128.0f,  0.066f,  0.3f};
    for (std::size_t index = 0; index < 9; ++index) {
        put_float(data, 0x9FC + 4 * index, jump[index]);
    }
    put_float(data, 0x9DC, 45.0f);
    const float side_jump[9] = {-3000.0f, -500.0f, -96.0f, 512.0f, 160.0f,
                                64.0f,    64.0f,   0.05f,  0.06f};
    for (std::size_t index = 0; index < 9; ++index) {
        put_float(data, 0xA50 + 4 * index, side_jump[index]);
    }
    const float forward[6] = {96.0f, 320.0f, 0.03f, 0.07f, 96.0f, 0.0f};
    const float side[6] = {72.0f, 192.0f, 0.066f, 0.25f, 96.0f, 256.0f};
    const float back[6] = {64.0f, 192.0f, 0.066f, 0.25f, 96.0f, 0.0f};
    for (std::size_t index = 0; index < 6; ++index) {
        put_float(data, 0xA20 + 4 * index, forward[index]);
        put_float(data, 0xA38 + 4 * index, side[index]);
        put_float(data, 0xA74 + 4 * index, back[index]);
    }
    put_float(data, 0xAC8, 2.5f);
    put_float(data, 0xACC, 2.6f);
    put_float(data, 0xAD0, 2.7f);
    put_float(data, 0xB14, 1.15f);
    return data;
}

void test_round_trip() {
    const bo2::game::KainTuning tuning = bo2::game::parse_kain_tuning(make_blob());
    CHECK(tuning.stick_deadzone == 0.1f);
    CHECK(tuning.jump.gravity_up == -2500.0f);
    CHECK(tuning.jump.gravity_down == -700.0f);
    CHECK(tuning.jump.gravity_glide == -128.0f);
    CHECK(tuning.jump.terminal_fall == 512.0f);
    CHECK(tuning.jump.terminal_glide == 160.0f);
    CHECK(tuning.jump.height_tap == 64.0f);
    CHECK(tuning.jump.height_hold == 128.0f);
    CHECK(tuning.jump.boost_start == 0.066f);
    CHECK(tuning.jump.boost_end == 0.3f);
    CHECK(tuning.turn_rate_ready == 2.5f);
    CHECK(tuning.turn_rate_run == 2.6f);
    CHECK(tuning.turn_rate_walk_back == 2.7f);
    CHECK(tuning.run_clip_rate == 1.15f);
    CHECK(tuning.jump_turn_rate == 45.0f);
    CHECK(tuning.side_jump.gravity_up == -3000.0f);
    CHECK(tuning.side_jump.gravity_down == -500.0f);
    CHECK(tuning.side_jump.height_tap == 64.0f);
    CHECK(tuning.side_jump.height_hold == 64.0f);
    CHECK(tuning.side_jump.boost_end == 0.06f);
    CHECK(tuning.air_forward.min_dist == 96.0f);
    CHECK(tuning.air_forward.max_dist == 320.0f);
    CHECK(tuning.air_forward.min_time == 0.03f);
    CHECK(tuning.air_forward.max_time == 0.07f);
    CHECK(tuning.air_forward.kd == 96.0f);
    CHECK(tuning.air_side.min_dist == 72.0f);
    CHECK(tuning.air_side.max_lateral_vel == 256.0f);
    CHECK(tuning.air_back.min_dist == 64.0f);
    CHECK(tuning.air_back.max_lateral_vel == 0.0f);
}

void put_u32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        data[offset + index] = static_cast<std::uint8_t>((value >> (8 * index)) & 0xFF);
    }
}

void append_u32(std::vector<std::uint8_t>& data, std::uint32_t value) {
    while (data.size() % 4 != 0) {
        data.push_back(0xFF);  // the padding byte of the real entry
    }
    data.resize(data.size() + 4);
    put_u32(data, data.size() - 4, value);
}

// The blob with a lore level table, two start records and the two lists that
// follow the blob in a real entry (FUN_007706c0).
std::vector<std::uint8_t> make_entry_with_tables() {
    std::vector<std::uint8_t> data = make_blob();
    const std::size_t table = 0xC00;   // three lore levels
    const std::size_t starts = 0xC40;  // two start records
    const std::size_t name = 0xD00;    // "slums"
    const float levels[6] = {6.0f, 30.0f, 30.0f, 60.0f, 950.0f, 500.0f};
    for (std::size_t index = 0; index < 6; ++index) {
        put_float(data, table + 4 * index, levels[index]);
    }
    put_u32(data, 4 + 0x8B0, 0x11223344);  // a stale pointer, as in the real file
    put_u32(data, 4 + 0x8B4, 3);
    put_u32(data, 4 + 0x8BC, 2);  // defaultHealth.loreLevel
    put_u32(data, 4 + 0x8DC, 0x55667788);
    put_u32(data, 4 + 0x8E0, 2);
    put_u32(data, 4 + starts + 4, 1);          // "slums" starts at level 1
    put_u32(data, 4 + starts + 0x24 + 4, 0);   // a record without a name
    const char text[] = "slums";
    for (std::size_t index = 0; index < sizeof text; ++index) {
        data[4 + name + index] = static_cast<std::uint8_t>(text[index]);
    }
    append_u32(data, 2);   // two names
    append_u32(data, 1);
    for (const char character : {'a', 'b', '\0'}) {
        data.push_back(static_cast<std::uint8_t>(character));
    }
    append_u32(data, 3);
    for (const char character : {'c', '\0'}) {
        data.push_back(static_cast<std::uint8_t>(character));
    }
    append_u32(data, 4);  // four fixups
    append_u32(data, 0x8B0);
    append_u32(data, static_cast<std::uint32_t>(table));
    append_u32(data, 0x8DC);
    append_u32(data, static_cast<std::uint32_t>(starts));
    append_u32(data, static_cast<std::uint32_t>(starts));
    append_u32(data, static_cast<std::uint32_t>(name));
    append_u32(data, 0x10);        // a target outside the blob is ignored (FUN_00780580)
    append_u32(data, 0x7FFFFFFF);
    return data;
}

template <typename Callback>
bool throws_parse_error(Callback callback) {
    try {
        callback();
    } catch (const bo2::common::ParseError&) {
        return true;
    }
    return false;
}

void test_rejects_bad_blobs() {
    // Shorter than the size field itself.
    CHECK(throws_parse_error([] { bo2::game::parse_kain_tuning({1, 2}); }));
    // Size field below the last movement field.
    std::vector<std::uint8_t> small = make_blob();
    small[0] = 0x10;
    small[1] = 0x00;
    CHECK(throws_parse_error([&] { bo2::game::parse_kain_tuning(small); }));
    // Size field larger than the data.
    std::vector<std::uint8_t> truncated = make_blob();
    truncated.resize(truncated.size() - 1);
    CHECK(throws_parse_error([&] { bo2::game::parse_kain_tuning(truncated); }));
    // Deadzone outside [0, 1).
    std::vector<std::uint8_t> bad_deadzone = make_blob();
    put_float(bad_deadzone, 0x00C, 1.0f);
    CHECK(throws_parse_error([&] { bo2::game::parse_kain_tuning(bad_deadzone); }));
    // Trailing bytes past the size field are allowed.
    std::vector<std::uint8_t> padded = make_blob();
    padded.resize(padded.size() + 64, 0);
    CHECK(bo2::game::parse_kain_tuning(padded).jump.height_tap == 64.0f);
}

void test_lore_tables_follow_the_fixups() {
    // Without the lists the pointers cannot be followed.
    const bo2::game::KainTuning bare = bo2::game::parse_kain_tuning(make_blob());
    CHECK(bare.lore_levels.empty());
    CHECK(bare.start_lore_levels.empty());

    const std::vector<std::uint8_t> entry = make_entry_with_tables();
    const bo2::game::KainTuning tuning = bo2::game::parse_kain_tuning(entry);
    CHECK(tuning.lore_levels.size() == 3);
    if (tuning.lore_levels.size() == 3) {
        CHECK(tuning.lore_levels[0].max_lore == 6.0f);
        CHECK(tuning.lore_levels[0].max_hit_points == 30.0f);
        CHECK(tuning.lore_levels[2].max_lore == 950.0f);
        CHECK(tuning.lore_levels[2].max_hit_points == 500.0f);
    }
    CHECK(tuning.default_lore_level == 2);
    CHECK(tuning.start_lore_levels.size() == 2);
    if (tuning.start_lore_levels.size() == 2) {
        CHECK(tuning.start_lore_levels[0].level == "slums");
        CHECK(tuning.start_lore_levels[0].lore_level == 1);
        CHECK(tuning.start_lore_levels[1].level.empty());
    }

    // FUN_004256c0: the record with the level's name, or the default.
    CHECK(bo2::game::start_lore_level(tuning, "slums") == 1);
    CHECK(bo2::game::start_lore_level(tuning, "keep") == 2);
    // FUN_004208a0 and FUN_00421020: the last record.
    CHECK(bo2::game::top_max_health(tuning) == 500.0f);
    CHECK(bo2::game::top_max_lore(tuning) == 950.0f);

    // FUN_00420ef0 and FUN_00420700: full health, the lore clamped to the level.
    bo2::game::KainVitals vitals;
    bo2::game::set_lore_level(vitals, tuning, 1, 12.0f);
    CHECK(vitals.lore_level == 1);
    CHECK(vitals.max_lore == 30.0f && vitals.max_lore_target == 30.0f);
    CHECK(vitals.lore == 12.0f && vitals.lore_target == 12.0f);
    CHECK(vitals.max_health == 60.0f && vitals.max_health_target == 60.0f);
    CHECK(vitals.health == 60.0f && vitals.health_target == 60.0f);
    bo2::game::set_lore_level(vitals, tuning, 0, 40.0f);
    CHECK(vitals.lore == 6.0f && vitals.lore_target == 6.0f);
    CHECK(vitals.health == 30.0f);
    bool rejected = false;
    try {
        bo2::game::set_lore_level(vitals, tuning, 3, 0.0f);
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    CHECK(rejected);
    rejected = false;
    try {
        bo2::game::top_max_health(bare);
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    CHECK(rejected);

    // A list that stops in the middle of a record.
    std::vector<std::uint8_t> cut = entry;
    cut.resize(cut.size() - 6);
    CHECK(throws_parse_error([&] { bo2::game::parse_kain_tuning(cut); }));
    // A table that runs past the blob.
    std::vector<std::uint8_t> long_table = entry;
    put_u32(long_table, 4 + 0x8B4, 0x1000);
    CHECK(throws_parse_error([&] { bo2::game::parse_kain_tuning(long_table); }));
}

void test_derived_jump_matches_the_original() {
    const bo2::game::KainTuning tuning = bo2::game::parse_kain_tuning(make_blob());
    const bo2::game::JumpDerived derived = bo2::game::derive_jump(tuning);
    CHECK(std::fabs(derived.launch_speed - 565.685f) < 1.0e-2f);
    CHECK(std::fabs(derived.boost_accel - 1998.94f) < 1.0e-2f);
    CHECK(std::fabs(derived.min_airtime - 0.6539f) < 1.0e-2f);
    CHECK(std::fabs(derived.boost_start - 0.066f) < 1.0e-4f);
    CHECK(std::fabs(derived.boost_end - 0.3f) < 1.0e-3f);
}

void test_derived_airtimes_match_the_original() {
    // FUN_00451d00 model[11] and model[12], cross-checked with
    // re/decomp/kain_movement/jump_model.py: 0.6539 and 1.0181.
    const bo2::game::KainTuning tuning = bo2::game::parse_kain_tuning(make_blob());
    const bo2::game::JumpDerived normal = bo2::game::derive_jump(tuning);
    CHECK(std::fabs(normal.min_airtime - 0.6539f) < 1.0e-3f);
    CHECK(std::fabs(normal.max_airtime - 1.0181f) < 1.0e-3f);
    // sideVertJump has equal heights: no boost and the launch of a 64 apex at g -3000.
    const bo2::game::JumpDerived side = bo2::game::derive_jump(tuning.side_jump);
    CHECK(side.boost_accel == 0.0f);
    CHECK(std::fabs(side.launch_speed - 619.677f) < 1.0e-2f);
    CHECK(std::fabs(side.max_airtime - 0.7125f) < 1.0e-3f);
}

void test_derived_air_steering_matches_the_original() {
    // FUN_00452030 on the three blocks, cross-checked with jump_model.py.
    const bo2::game::KainTuning tuning = bo2::game::parse_kain_tuning(make_blob());
    const bo2::game::JumpDerived normal = bo2::game::derive_jump(tuning);
    const bo2::game::AirSteerDerived forward = bo2::game::derive_air_steer(tuning.air_forward, normal);
    CHECK(std::fabs(forward.launch_speed - 146.813f) < 1.0e-2f);
    CHECK(std::fabs(forward.accel - 4403.56f) < 0.5f);
    CHECK(std::fabs(forward.peak_speed - 322.956f) < 1.0e-2f);
    CHECK(std::fabs(forward.damping - 0.96f) < 1.0e-6f);
    CHECK(forward.lateral_speed == 0.0f);
    const bo2::game::AirSteerDerived back = bo2::game::derive_air_steer(tuning.air_back, normal);
    CHECK(std::fabs(back.launch_speed - 97.8755f) < 1.0e-2f);
    CHECK(std::fabs(back.accel - 583.535f) < 0.05f);
    CHECK(std::fabs(back.peak_speed - 205.246f) < 1.0e-2f);
    const bo2::game::AirSteerDerived side = bo2::game::derive_air_steer(tuning.air_side, normal);
    CHECK(std::fabs(side.launch_speed - 110.110f) < 1.0e-2f);
    CHECK(std::fabs(side.accel - 504.829f) < 0.05f);
    CHECK(std::fabs(side.peak_speed - 202.998f) < 1.0e-2f);
    CHECK(side.lateral_speed == 256.0f);
}

void test_air_steering_caps_the_speed_by_the_longest_distance() {
    // max_dist below what the launch speed covers in the longest airtime:
    // the speed is cut to max_dist / max_airtime and no ramp is needed.
    const bo2::game::KainTuning tuning = bo2::game::parse_kain_tuning(make_blob());
    const bo2::game::JumpDerived normal = bo2::game::derive_jump(tuning);
    bo2::game::AirSteerTuning block = tuning.air_forward;
    block.max_dist = 100.0f;
    const bo2::game::AirSteerDerived derived = bo2::game::derive_air_steer(block, normal);
    CHECK(std::fabs(derived.launch_speed - 100.0f / normal.max_airtime) < 1.0e-3f);
    CHECK(std::fabs(derived.accel) < 1.0e-3f);
    CHECK(std::fabs(derived.peak_speed - derived.launch_speed) < 1.0e-3f);
    // A ramp of no length leaves the speed alone.
    block = tuning.air_forward;
    block.max_time = block.min_time;
    CHECK(bo2::game::derive_air_steer(block, normal).accel == 0.0f);
    // Without airtimes there is nothing to divide by.
    bool threw = false;
    try {
        bo2::game::derive_air_steer(tuning.air_forward, bo2::game::JumpDerived{});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

void test_equal_heights_disable_the_boost() {
    bo2::game::KainTuning tuning = bo2::game::parse_kain_tuning(make_blob());
    tuning.jump.height_hold = tuning.jump.height_tap;
    const bo2::game::JumpDerived derived = bo2::game::derive_jump(tuning);
    CHECK(derived.boost_accel == 0.0f);
    CHECK(std::fabs(derived.launch_speed - 565.685f) < 1.0e-2f);
}

void test_collapsed_boost_window_disables_the_boost() {
    // boost_start = 0.05 and a hold height of exactly 0.05 * vz(0.05) leave the
    // boost window with no length (FUN_00451d00 then divides by it).
    bo2::game::KainTuning tuning = bo2::game::parse_kain_tuning(make_blob());
    tuning.jump.boost_start = 0.05f;
    tuning.jump.boost_end = 0.04f;
    tuning.jump.height_hold = 0.05f * (565.6854f - 2500.0f * 0.05f);
    const bo2::game::JumpDerived derived = bo2::game::derive_jump(tuning);
    CHECK(derived.boost_accel == 0.0f);
    CHECK(std::isfinite(derived.boost_end));
    CHECK(derived.boost_end == derived.boost_start);
    CHECK(std::fabs(derived.launch_speed - 565.685f) < 1.0e-2f);
}

void test_early_return_case_disables_the_boost() {
    // The hold height is reached before the tap apex time: FUN_00451d00 takes
    // its early return, which the port treats as no boost.
    bo2::game::KainTuning tuning = bo2::game::parse_kain_tuning(make_blob());
    tuning.jump.boost_start = 0.1f;
    tuning.jump.height_hold = 0.1f * (565.6854f - 2500.0f * 0.1f);
    const bo2::game::JumpDerived derived = bo2::game::derive_jump(tuning);
    CHECK(derived.boost_accel == 0.0f);
    CHECK(derived.boost_end == derived.boost_start);
}

void test_invalid_jump_tuning_throws() {
    bo2::game::KainTuning tuning = bo2::game::parse_kain_tuning(make_blob());
    tuning.jump.gravity_up = 0.0f;
    bool threw = false;
    try {
        bo2::game::derive_jump(tuning);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

}  // namespace

int main() {
    test_round_trip();
    test_rejects_bad_blobs();
    test_lore_tables_follow_the_fixups();
    test_derived_jump_matches_the_original();
    test_derived_airtimes_match_the_original();
    test_derived_air_steering_matches_the_original();
    test_air_steering_caps_the_speed_by_the_longest_distance();
    test_equal_heights_disable_the_boost();
    test_collapsed_boost_window_disables_the_boost();
    test_early_return_case_disables_the_boost();
    test_invalid_jump_tuning_throws();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "kain tuning tests: ok\n";
    return 0;
}
