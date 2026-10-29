#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "bo2/common/reader.hpp"
#include "bo2/game/kain_tuning.hpp"

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
