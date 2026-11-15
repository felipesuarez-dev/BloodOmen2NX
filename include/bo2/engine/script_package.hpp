#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace bo2::engine {

// A compiled script package (`bpackage` entry, and the `packages` section of
// a region). Layout: docs/FORMATS/bpackage.md, read from the package writer
// of the executable (FUN_0058cbb0, FUN_0058d000, FUN_0058d420).

// A type (FUN_0058ce80): kind 6 is an array of `inner[0]` with `count`
// elements, kind 7 wraps `inner[0]`, kinds 9, 10 and 11 name a class or an
// enumeration; the other kinds are simple values.
struct ScriptType {
    std::uint16_t kind = 0;
    std::string name;
    std::vector<ScriptType> inner;
    std::uint32_t count = 0;
};

// A parameter of a method or a local of a body: its type and its offset in
// the locals of a call.
struct ScriptVariable {
    ScriptType type;
    std::uint32_t offset = 0;
};

struct ScriptEnum {
    std::string name;
    std::uint16_t word = 0;
    std::vector<std::pair<std::string, std::uint32_t>> values;
};

// A field and its default value (FUN_0058cf20): `default_flag` and
// `default_value` for kinds 1, 2, 3, 5 and 11, `default_name` for kind 4 when
// the flag is 1, `default_object` (the raw chunk) for kind 9 when the flag
// is 1.
struct ScriptField {
    std::string name;
    std::uint16_t flags = 0;
    std::uint32_t offset = 0;
    ScriptType type;
    std::uint16_t default_flag = 0;
    std::uint32_t default_value = 0;
    std::string default_name;
    std::vector<std::uint8_t> default_object;
};

inline constexpr std::uint16_t kScriptNoSlot = 0xFFFF;

struct ScriptMethod {
    std::string name;
    std::uint8_t byte4 = 0;
    std::uint8_t byte5 = 0;  // 2 for the methods that run as a thread
    std::uint16_t slot = kScriptNoSlot;
    ScriptType result;
    std::vector<ScriptVariable> params;
};

struct ScriptBody {
    std::uint16_t method = 0;
    std::uint16_t flags = 0;
    std::uint16_t state = 0;
    std::uint16_t locals_size = 0;
    std::uint16_t stack_size = 0;  // in 4-byte cells
    std::vector<ScriptVariable> locals;
    std::vector<std::uint32_t> code;

    bool native() const { return (flags & 1U) != 0; }
};

// One entry of the slot list of the link record: in `state`, `slot` runs the
// body `body` of the class `class_name`.
struct ScriptSlot {
    std::uint16_t state = 0;
    std::uint16_t slot = 0;
    std::string class_name;
    std::uint16_t body = 0;
};

// One reference of a body (link record, FUN_0058d420): the code word `word`
// takes a name (kinds 0 and 1), a class (2), a method (3) or a body (4) of
// the class `name`, or (5) the state tables of `name` and the method `index`
// of `second`.
struct ScriptReference {
    std::uint16_t kind = 0;
    std::uint16_t word = 0;
    std::string name;
    std::string second;
    std::uint16_t index = 0;
};

struct ScriptClass {
    std::string name;
    std::uint16_t kind = 0;
    std::uint16_t data_size = 0;
    std::uint16_t slot_count = 0;
    std::vector<std::string> states;
    std::vector<ScriptEnum> enums;
    std::vector<ScriptField> fields;
    std::vector<ScriptMethod> methods;
    std::vector<ScriptBody> bodies;
    std::string parent;                     // empty: no parent
    std::vector<std::string> state_owners;  // per state; empty: own table
    std::vector<ScriptSlot> slots;
    std::vector<std::vector<ScriptReference>> references;  // one per body
};

struct ScriptPackage {
    std::uint16_t version = 0;
    std::vector<std::string> imports;
    std::vector<ScriptClass> classes;
};

// Reads a package. Name number i of the payload is names[symbols[i]], as in
// parse_cell_group. Throws common::ParseError on any of the validation rules
// of docs/FORMATS/bpackage.md (version, name indices, chunk bounds, reference
// kinds, method and state indices, slots, reference targets, trailing bytes).
ScriptPackage parse_script_package(const std::vector<std::uint8_t>& data,
                                   const std::vector<std::uint16_t>& symbols,
                                   const std::vector<std::string>& names);

// Words of the instruction that starts with `word` (FUN_005b8b80): 2 for the
// codes 0x02, 0x21, 0x26, 0x47, 0x48, 0x4a to 0x4e, 0x50 to 0x54 and 0x56
// to 0x58, 3 for 0x49, 0x4f and 0x55, 1 for the others.
std::size_t script_instruction_words(std::uint32_t word);

}  // namespace bo2::engine
