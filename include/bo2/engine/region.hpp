#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bo2/engine/math.hpp"
#include "bo2/engine/script_package.hpp"

namespace bo2::engine {

// A property of an entity (docs/FORMATS/region.md, "Region entity table"):
// its key, its type, and its value, the text for type 8 and the number for
// the other types.
struct RegionProperty {
    std::string key;
    std::uint32_t type = 0;
    std::uint32_t number = 0;
    std::string text;
};

struct RegionEntity {
    std::uint32_t flags = 0;
    std::string name;
    std::string model;
    Vec3 position{0.0f, 0.0f, 0.0f};
    std::vector<RegionProperty> properties;  // in the order of the file
};

struct RegionEntityTable {
    std::uint32_t package = 0;
    std::uint32_t marker = 0;
    std::vector<RegionEntity> entities;
};

std::vector<RegionEntityTable> parse_region_entities(const std::vector<std::uint8_t>& data);

// The class of the script object of an entity: the value of its property
// `reserved.script`, empty when it has none (FUN_007828c0).
std::string reserved_script(const RegionEntity& entity);

// The name of the script package of a region from the name of its entry
// (`slums/sl01` gives `slums_sl01`, FUN_00783900): the characters before the
// first letter are dropped, then the trailing spaces, tabs, carriage returns
// and line feeds; every character that is not a letter or a digit becomes
// `_`, and the letters become lower case (the C locale).
std::string region_package_name(const std::string& region_name);

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
