#pragma once

#include <cstdint>

namespace bo2::engine {

// The rand of the C library linked into the game (FUN_007a4289;
// docs/GAMEPLAY.md, "Random numbers"): state = state * 214013 + 2531011,
// the value is bits 16 to 30 of the state. The state starts at 1 and the
// game never seeds it again.
class CrtRandom {
public:
    explicit CrtRandom(std::uint32_t state = 1) : state_(state) {}
    std::uint32_t next();
    std::uint32_t state() const { return state_; }

private:
    std::uint32_t state_;
};

}  // namespace bo2::engine
