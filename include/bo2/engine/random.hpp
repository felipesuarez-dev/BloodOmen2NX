#pragma once

#include <cstdint>

namespace bo2::engine {

// The game's global random generator (FUN_00694750, FUN_00694760, FUN_006947e0
// and FUN_00694820; docs/GAMEPLAY.md, "Random numbers"). It is an additive
// lagged generator: k = 24, j = 55 and a table T[1..55] seeded with 12345.
class GameRandom {
public:
    explicit GameRandom(std::uint32_t seed = 12345);

    // FUN_006947e0: fills T from the seed and restarts k and j.
    void seed(std::uint32_t seed_value);

    // FUN_00694820: the next 32-bit value.
    std::uint32_t next();

private:
    std::uint32_t k_ = 24;
    std::uint32_t j_ = 55;
    std::uint32_t table_[56] = {};  // T[1..55]; T[0] is never seeded or read
};

}  // namespace bo2::engine
