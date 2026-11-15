#include "bo2/engine/crt_random.hpp"

namespace bo2::engine {

// FUN_007a4289.
std::uint32_t CrtRandom::next() {
    state_ = state_ * 214013U + 2531011U;
    return (state_ >> 16) & 0x7fffU;
}

}  // namespace bo2::engine
