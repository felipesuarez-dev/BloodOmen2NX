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
constexpr std::size_t kJumpBlock = 0x9FC;
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
    tuning.jump.gravity_up = read_float(reader, kJumpBlock + 0x00);
    tuning.jump.gravity_down = read_float(reader, kJumpBlock + 0x04);
    tuning.jump.gravity_glide = read_float(reader, kJumpBlock + 0x08);
    tuning.jump.terminal_fall = read_float(reader, kJumpBlock + 0x0C);
    tuning.jump.terminal_glide = read_float(reader, kJumpBlock + 0x10);
    tuning.jump.height_tap = read_float(reader, kJumpBlock + 0x14);
    tuning.jump.height_hold = read_float(reader, kJumpBlock + 0x18);
    tuning.jump.boost_start = read_float(reader, kJumpBlock + 0x1C);
    tuning.jump.boost_end = read_float(reader, kJumpBlock + 0x20);
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
JumpDerived derive_jump(const KainTuning& tuning) {
    const JumpTuning& jump = tuning.jump;
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
    return derived;
}

}  // namespace bo2::game
