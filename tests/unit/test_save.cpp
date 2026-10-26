#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "bo2/game/save.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

void test_round_trip() {
    bo2::game::SaveGame save;
    save.level = "sanctuary";
    save.player.position = {1.5f, -2.0f, 3.25f};
    save.player.yaw = 0.75f;
    save.player.pitch = -0.25f;
    save.player.vertical_velocity = 2.0f;
    save.player.grounded = true;
    save.tick = 12345;
    const auto bytes = bo2::game::serialize_save(save);
    const bo2::game::SaveGame loaded = bo2::game::deserialize_save(bytes);
    CHECK(loaded.level == save.level);
    CHECK(loaded.player.position.x == save.player.position.x);
    CHECK(loaded.player.position.z == save.player.position.z);
    CHECK(loaded.player.yaw == save.player.yaw);
    CHECK(loaded.player.grounded);
    CHECK(loaded.tick == save.tick);
    CHECK(bo2::game::save_hash(bytes) == bo2::game::save_hash(bytes));
}

void test_checkpoints() {
    bo2::game::CheckpointSlots slots;
    bo2::game::SaveGame save;
    save.level = "test";
    CHECK(slots.store(0, save));
    CHECK(!slots.store(8, save));
    CHECK(slots.has(0));
    CHECK(slots.count() == 1);
    CHECK(slots.load(0)->level == "test");
    CHECK(!slots.load(1).has_value());
}

void test_rejects_corrupt() {
    bool threw = false;
    try {
        bo2::game::deserialize_save({1, 2, 3});
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
    auto bytes = bo2::game::serialize_save({});
    bytes.pop_back();
    threw = false;
    try {
        bo2::game::deserialize_save(bytes);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

}  // namespace

int main() {
    test_round_trip();
    test_checkpoints();
    test_rejects_corrupt();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "save tests: ok\n";
    return 0;
}
