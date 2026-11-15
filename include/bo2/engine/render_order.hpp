#pragma once

#include <cstdint>

namespace bo2::engine {

// FUN_0056d130 and FUN_0056ce80: the sort value of a mesh in the sorted pass.
// `render_priority` is the model's RenderPriority, `sort_bias` its SortBias,
// `depth` the depth of the instance origin along the view direction and
// `sky_sphere` whether the model has SkySphere. A value with priority 0 (or
// a SkySphere model) is ordered by depth; the others are not.
float sort_value(float render_priority, float sort_bias, float depth, bool sky_sphere);

// FUN_00562e60: the 24-bit sort key of a sort value. The value is scaled by
// the float at 0xc1063737 in double precision, truncated toward zero, folded
// around 0x800000 and clamped to the 24-bit range.
std::uint32_t sort_key(float value);

}  // namespace bo2::engine
