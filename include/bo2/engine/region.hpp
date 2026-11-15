#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bo2/engine/math.hpp"
#include "bo2/engine/script_package.hpp"

namespace bo2::engine {

struct RegionEntity {
    std::uint32_t flags = 0;
    std::string name;
    std::string model;
    Vec3 position{0.0f, 0.0f, 0.0f};
};

struct RegionEntityTable {
    std::uint32_t package = 0;
    std::uint32_t marker = 0;
    std::vector<RegionEntity> entities;
};

std::vector<RegionEntityTable> parse_region_entities(const std::vector<std::uint8_t>& data);

// Reads the script package of the `packages` section of a region entry
// (docs/FORMATS/bpackage.md, "Region packages"). `symbols` and `names` are
// those of the entry, as for parse_script_package. Throws common::ParseError
// when the region has no `packages` section, when the section does not hold
// exactly one package or when the package size does not reach the end of the
// section.
ScriptPackage parse_region_script_package(const std::vector<std::uint8_t>& data,
                                          const std::vector<std::uint16_t>& symbols,
                                          const std::vector<std::string>& names);

}  // namespace bo2::engine
