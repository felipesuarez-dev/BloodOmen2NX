#include "bo2/engine/random.hpp"

namespace bo2::engine {

namespace {

constexpr std::uint32_t kInitialK = 24;  // FUN_00694760
constexpr std::uint32_t kInitialJ = 55;  // FUN_00694760
constexpr std::uint32_t kWrap = 55;      // the last index of T[1..55]
constexpr std::uint64_t kMultiplier = 16807;
constexpr std::uint64_t kModulus = 0x7FFFFFFF;

}  // namespace

GameRandom::GameRandom(std::uint32_t seed_value) {
    seed(seed_value);
}

void GameRandom::seed(std::uint32_t seed_value) {
    // FUN_006947e0: v = seed; for i from 55 down to 1, T[i] = v and
    // v = (v * 16807) mod 0x7fffffff in 64-bit arithmetic.
    std::uint64_t value = seed_value;
    for (std::uint32_t index = 55; index >= 1; --index) {
        table_[index] = static_cast<std::uint32_t>(value);
        value = (value * kMultiplier) % kModulus;
    }
    k_ = kInitialK;
    j_ = kInitialJ;
}

std::uint32_t GameRandom::next() {
    // FUN_00694820: i = j, j -= 1 and T[i] += T[k] with a 32-bit wrap. When j
    // reaches 0 it becomes 55 and k is not touched; otherwise k -= 1 and it
    // becomes 55 when it reaches 0. The value is T[i].
    const std::uint32_t i = j_;
    j_ = j_ - 1;
    table_[i] += table_[k_];
    const std::uint32_t previous_k = k_;
    k_ = k_ - 1;
    if (j_ == 0) {
        j_ = kWrap;
    }
    // Both counters wrap in the same call every 1,320 draws. FUN_00694820
    // returns before the k reset when j reaches 0; at that one intersection
    // that would leave k at 0 and the next draw would index out of the table.
    // The reset stays unconditional so the index is always 1..55.
    if (previous_k - 1 == 0) {
        k_ = kWrap;
    }
    return table_[i];
}

}  // namespace bo2::engine
