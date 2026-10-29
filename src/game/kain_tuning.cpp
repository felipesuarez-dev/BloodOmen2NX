#include "bo2/game/kain_tuning.hpp"

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#include "bo2/common/reader.hpp"
#include "bo2/engine/anim_reel.hpp"

namespace bo2::game {

namespace {

// B offsets inside the data blob; the file offset is 4 + the B offset.
constexpr std::size_t kBlobStart = 4;
constexpr std::size_t kStickDeadzone = 0x00C;
constexpr std::size_t kAirTurnRate = 0x9DC;
constexpr std::size_t kJumpBlock = 0x9FC;
constexpr std::size_t kAirForward = 0xA20;
constexpr std::size_t kAirSide = 0xA38;
constexpr std::size_t kSideJumpBlock = 0xA50;
constexpr std::size_t kAirBack = 0xA74;
constexpr std::size_t kTurnReady = 0xAC8;
constexpr std::size_t kTurnRun = 0xACC;
constexpr std::size_t kTurnWalkBack = 0xAD0;
constexpr std::size_t kRunClipRate = 0xB14;
// One past the last field we read.
constexpr std::size_t kBlobMinimum = kRunClipRate + 4;

float read_float(const common::Reader& reader, std::size_t blob_offset) {
    const std::uint32_t bits = reader.u32(kBlobStart + blob_offset);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

JumpTuning read_jump(const common::Reader& reader, std::size_t block) {
    JumpTuning jump;
    jump.gravity_up = read_float(reader, block + 0x00);
    jump.gravity_down = read_float(reader, block + 0x04);
    jump.gravity_glide = read_float(reader, block + 0x08);
    jump.terminal_fall = read_float(reader, block + 0x0C);
    jump.terminal_glide = read_float(reader, block + 0x10);
    jump.height_tap = read_float(reader, block + 0x14);
    jump.height_hold = read_float(reader, block + 0x18);
    jump.boost_start = read_float(reader, block + 0x1C);
    jump.boost_end = read_float(reader, block + 0x20);
    return jump;
}

AirSteerTuning read_air_steer(const common::Reader& reader, std::size_t block) {
    AirSteerTuning steer;
    steer.min_dist = read_float(reader, block + 0x00);
    steer.max_dist = read_float(reader, block + 0x04);
    steer.min_time = read_float(reader, block + 0x08);
    steer.max_time = read_float(reader, block + 0x0C);
    steer.kd = read_float(reader, block + 0x10);
    steer.max_lateral_vel = read_float(reader, block + 0x14);
    return steer;
}

// |forward root speed| of the named animreel entry, in u/s.
float clip_root_speed(const bo2::engine::BigArchive& archive, const char* name) {
    const bo2::engine::BigEntry* entry = archive.find("animreel", name);
    if (entry == nullptr) {
        throw common::ParseError(std::string("no animreel entry named ") + name);
    }
    const bo2::engine::AnimReel reel = bo2::engine::parse_anim_reel(archive.extract(*entry));
    if (!(reel.duration > 0.0f)) {
        throw common::ParseError(std::string("animreel ") + name + " has no duration");
    }
    return std::fabs(bo2::engine::root_displacement(reel).x) / reel.duration;
}

}  // namespace

KainTuning parse_kain_tuning(const std::vector<std::uint8_t>& bytes) {
    const common::Reader reader(bytes);
    const std::size_t blob_size = reader.u32(0);
    if (blob_size < kBlobMinimum) {
        throw common::ParseError("kain tunedata size field is too small for the movement fields");
    }
    if (bytes.size() - kBlobStart < blob_size) {
        throw common::ParseError("kain tunedata is shorter than its size field");
    }
    KainTuning tuning;
    tuning.stick_deadzone = read_float(reader, kStickDeadzone);
    tuning.jump_turn_rate = read_float(reader, kAirTurnRate);
    tuning.jump = read_jump(reader, kJumpBlock);
    tuning.side_jump = read_jump(reader, kSideJumpBlock);
    tuning.air_forward = read_air_steer(reader, kAirForward);
    tuning.air_side = read_air_steer(reader, kAirSide);
    tuning.air_back = read_air_steer(reader, kAirBack);
    tuning.turn_rate_ready = read_float(reader, kTurnReady);
    tuning.turn_rate_run = read_float(reader, kTurnRun);
    tuning.turn_rate_walk_back = read_float(reader, kTurnWalkBack);
    tuning.run_clip_rate = read_float(reader, kRunClipRate);
    if (!(tuning.stick_deadzone >= 0.0f && tuning.stick_deadzone < 1.0f)) {
        throw common::ParseError("kain tunedata stick deadzone is outside [0, 1)");
    }
    return tuning;
}

void apply_kain_clip_speeds(KainTuning& tuning, const bo2::engine::BigArchive& archive) {
    tuning.run_root_speed = clip_root_speed(archive, "kain/run");
    tuning.walk_back_root_speed = clip_root_speed(archive, "kain/walk_backwards");
}

// Port of FUN_00451d00. The original runs in x87 single precision; this runs
// in double, which agrees to well under 1e-3 on the shipped values.
JumpDerived derive_jump(const JumpTuning& jump) {
    if (!(jump.gravity_up < 0.0f) || !(jump.gravity_down < 0.0f) || !(jump.height_tap > 0.0f)) {
        throw std::invalid_argument("jump tuning needs negative gravities and a positive tap height");
    }
    const double g_up = jump.gravity_up;
    const double h_tap = jump.height_tap;
    const double h_hold = jump.height_hold;
    double t_min = jump.boost_start;
    double t_max = jump.boost_end;

    const double t_apex = std::sqrt(h_tap * -2.0 / g_up);
    const double launch = -(t_apex * g_up);
    if (t_apex < t_min) {
        t_min = t_apex;
    }

    double boost = 0.0;
    double apex_time = t_apex;  // param_2 of FUN_00451d00: the solved end of the boost, else the tap apex time
    bool boosted = h_tap != h_hold;
    if (boosted) {
        const double limit = h_hold / (t_min * g_up + launch);
        if (t_max <= limit) {
            if (t_max <= t_apex) {
                t_max = t_apex + t_apex;
            }
            if (limit < t_max) {
                t_max = limit;
            }
        } else {
            t_max = limit;
            if (limit <= t_apex) {
                // FUN_00451d00 returns early here with a different layout.
                // The shipped tuning never reaches it; the port boosts nothing.
                boosted = false;
            }
        }
    }
    if (boosted) {
        // Fixed-point search for the end of the boost window (at most 100 rounds).
        const double half_gravity = g_up * -0.5;
        double window = 0.0;
        double root = 0.0;
        for (int rounds = 100; rounds > 0; --rounds) {
            window = t_max - t_min;
            const double middle = t_max - window * 0.5;
            const double slope = g_up * middle;
            const double constant = (middle * launch - h_hold) * half_gravity * 4.0;
            if (0.0 <= constant) {
                boosted = false;
                break;
            }
            root = (std::sqrt(slope * slope - constant) - slope) / (half_gravity + half_gravity);
            if (!(root < t_max)) {
                break;
            }
            t_max = root;
        }
        if (boosted) {
            boost = (-(root * g_up) - launch) / window;
            boosted = std::isfinite(boost);
            apex_time = root;
        }
    }
    if (!boosted) {
        boost = 0.0;
        t_max = t_min;
    }

    JumpDerived derived;
    derived.launch_speed = static_cast<float>(launch);
    derived.boost_accel = static_cast<float>(boost);
    derived.boost_start = static_cast<float>(t_min);
    derived.boost_end = static_cast<float>(t_max);
    derived.min_airtime =
        static_cast<float>(t_apex + std::sqrt(h_tap / jump.gravity_down * -2.0));
    // FUN_00451d00 model[12]: the time to the boosted apex plus the fall from h2.
    derived.max_airtime =
        static_cast<float>(apex_time + std::sqrt(h_hold / jump.gravity_down * -2.0));
    return derived;
}

JumpDerived derive_jump(const KainTuning& tuning) {
    return derive_jump(tuning.jump);
}

// Port of FUN_00452030 (the part the air control reads; the glide speed it
// also stores in block[11] is not used yet).
AirSteerDerived derive_air_steer(const AirSteerTuning& block, const JumpDerived& normal) {
    if (!(normal.min_airtime > 0.0f) || !(normal.max_airtime > 0.0f)) {
        throw std::invalid_argument("air steering needs the airtimes of the normal jump");
    }
    const double shortest = normal.min_airtime;
    const double longest = normal.max_airtime;
    const double ramp = static_cast<double>(block.max_time) - block.min_time;
    // The speed that covers min_dist in the shortest airtime, capped so the
    // longest airtime stays inside max_dist.
    double speed = block.min_dist / shortest;
    if (block.max_dist < longest * speed) {
        speed = block.max_dist / longest;
    }
    // The extra acceleration over the ramp that makes up the rest of max_dist.
    double accel = 0.0;
    if (1.0e-5 <= std::fabs(ramp)) {
        accel = (block.max_dist - longest * speed) /
                ((ramp * 0.5 + (longest - block.max_time)) * ramp);
    }
    AirSteerDerived derived;
    derived.launch_speed = static_cast<float>(speed);
    derived.accel = static_cast<float>(accel);
    derived.peak_speed = static_cast<float>(ramp * accel + speed);
    derived.damping = block.kd * 0.01f;
    derived.lateral_speed = block.max_lateral_vel;
    return derived;
}

}  // namespace bo2::game
