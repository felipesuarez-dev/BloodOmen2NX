#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "bo2/engine/anim_reel.hpp"
#include "bo2/engine/big_archive.hpp"
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

}  // namespace

int main() {
    const char* root = std::getenv("BO2NX_ASSETS_DIR");
    if (root == nullptr) {
        std::cout << "kain asset test skipped (BO2NX_ASSETS_DIR is unset)\n";
        return 0;
    }
    const std::filesystem::path path =
        std::filesystem::path(root) / "media/gogames/bo2/resources/levels/kain/kain.big";
    if (!std::filesystem::exists(path)) {
        std::cout << "kain asset test skipped (kain.big not found)\n";
        return 0;
    }
    try {
        const bo2::engine::BigArchive archive = bo2::engine::BigArchive::load(path.string());
        const bo2::engine::BigEntry* entry = archive.find("tunedata", "kain");
        CHECK(entry != nullptr);
        if (entry != nullptr) {
            const std::vector<std::uint8_t> bytes = archive.extract(*entry);
            CHECK(bytes.size() > 4);
            CHECK(bytes[0] == 0xC5 && bytes[1] == 0x2E && bytes[2] == 0x00 && bytes[3] == 0x00);
            bo2::game::KainTuning tuning = bo2::game::parse_kain_tuning(bytes);
            CHECK(std::fabs(tuning.stick_deadzone - 0.1f) < 1.0e-6f);
            CHECK(tuning.turn_rate_ready == 2.5f);
            CHECK(tuning.turn_rate_run == 2.5f);
            CHECK(tuning.turn_rate_walk_back == 2.5f);
            CHECK(tuning.jump.gravity_up == -2500.0f);
            CHECK(tuning.jump.gravity_down == -700.0f);
            CHECK(tuning.jump.terminal_fall == 512.0f);
            CHECK(tuning.jump.height_tap == 64.0f);
            CHECK(tuning.jump.height_hold == 128.0f);
            CHECK(std::fabs(tuning.run_clip_rate - 1.15f) < 1.0e-6f);
            bo2::game::apply_kain_clip_speeds(tuning, archive);
            CHECK(std::fabs(tuning.run_root_speed - 200.0f) < 1.0e-3f);
            CHECK(std::fabs(tuning.walk_back_root_speed - 62.5f) < 1.0e-3f);
            // FUN_00445ba0 at full stick and the walk_backwards clip node (speed 1.7).
            CHECK(std::fabs(tuning.run_root_speed * tuning.run_clip_rate - 230.0f) < 1.0e-2f);
            CHECK(std::fabs(tuning.walk_back_root_speed * 1.7f - 106.25f) < 1.0e-2f);
            const bo2::game::JumpDerived derived = bo2::game::derive_jump(tuning);
            CHECK(std::fabs(derived.launch_speed - 565.685f) < 1.0e-2f);
            CHECK(std::fabs(derived.boost_accel - 1998.94f) < 1.0e-2f);
            CHECK(std::fabs(derived.min_airtime - 0.6539f) < 1.0e-2f);
        }
        const bo2::engine::BigEntry* walk = archive.find("animreel", "kain/walk");
        CHECK(walk != nullptr);
        if (walk != nullptr) {
            const bo2::engine::AnimReel reel =
                bo2::engine::parse_anim_reel(archive.extract(*walk));
            CHECK(reel.frames == 47);
            // Cross-check against tools/bo2fmt/animreel.py: displacement 86.4010.
            CHECK(std::fabs(bo2::engine::root_displacement(reel).x - 86.4010f) < 1.0e-3f);
            CHECK(std::fabs(bo2::engine::root_displacement(reel).x / reel.duration - 56.3485f) <
                  1.0e-3f);
        }
    } catch (const std::exception& error) {
        std::cerr << "kain asset test: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "kain asset tests: ok\n";
    return 0;
}
