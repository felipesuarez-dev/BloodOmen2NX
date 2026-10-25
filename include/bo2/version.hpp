#pragma once

#include <string_view>

namespace bo2 {

inline constexpr std::string_view kProjectName = "Blood Omen 2 NX";
inline constexpr std::string_view kVersion = "0.1.0";

std::string_view version();

}  // namespace bo2
