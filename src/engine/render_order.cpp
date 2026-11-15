#include "bo2/engine/render_order.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace bo2::engine {

namespace {

// The float at 0xc1063737: -8.388480186462402.
float sort_factor() {
    const std::uint32_t bits = 0xC1063737U;
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

}  // namespace

// FUN_0056d130 and FUN_0056ce80: a negative priority goes to 100000 plus the
// priority, a positive one to -100000 minus the priority, and priority 0 to
// the SortBias. Only priority 0 with depth is ordered by depth; SkySphere
// adds 100000 and turns the depth off in every case.
float sort_value(float render_priority, float sort_bias, float depth, bool sky_sphere) {
    float base = 0.0f;
    bool by_depth = false;
    if (render_priority == 0.0f) {
        base = sort_bias;
        by_depth = true;
    } else if (render_priority < 0.0f) {
        base = render_priority + 100000.0f;
    } else {
        base = -100000.0f - render_priority;
    }
    if (sky_sphere) {
        base += 100000.0f;
        by_depth = false;
    }
    return by_depth ? depth + base : base;
}

// FUN_00562e60: the value is multiplied by the float at 0xc1063737 in double
// precision and truncated toward zero. The result is folded and clamped, and
// the key is its complement in 24 bits.
std::uint32_t sort_key(float value) {
    const double scaled = static_cast<double>(value) * static_cast<double>(sort_factor());
    const std::int32_t whole = static_cast<std::int32_t>(scaled);
    const std::int32_t folded = std::clamp(0x800000 - whole, 0, 0xFFFFFF);
    return static_cast<std::uint32_t>(0xFFFFFF - folded);
}

}  // namespace bo2::engine
