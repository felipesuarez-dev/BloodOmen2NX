#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "bo2/engine/script_package.hpp"

namespace bo2::engine {

// The packages of a region linked into one program (docs/SCRIPTS.md,
// "Loading and linking"; FUN_0058c060). Classes, methods and bodies get
// program-wide ids in link order: the packages in the order given, the
// classes in the order of their package, the methods and bodies in the order
// of their class. A field that refers to an id holds the id + 1, so 0 stays
// "none".
struct ScriptProgramClass {
    std::string name;
    std::uint32_t parent = 0;  // id + 1 of the parent class, 0 for none
    const ScriptClass* source = nullptr;
    // One slot table per state: states[state][slot] is the id + 1 of the
    // body that runs, 0 for an empty slot.
    std::vector<std::vector<std::uint32_t>> states;
    std::vector<std::uint32_t> methods;  // program ids of the methods
    std::vector<std::uint32_t> bodies;   // program ids of the bodies
    bool value_class = false;            // class kind 5
};

struct ScriptProgramMethod {
    std::uint32_t owner = 0;  // class id
    std::uint16_t index = 0;  // index in the class
    const ScriptMethod* source = nullptr;
};

struct ScriptProgramBody {
    std::uint32_t owner = 0;  // class id
    std::uint16_t index = 0;  // index in the class
    const ScriptBody* source = nullptr;
    // The code of the body with the words of its references rewritten.
    std::vector<std::uint32_t> code;
};

class ScriptProgram {
public:
    // Links `packages` in order. `native_types` names the value types that
    // have no class (in the game data: activation_info and creature).
    // Throws ScriptError when a parent, a state owner or the class of a slot
    // entry or a reference does not exist, when a state owner is linked after
    // the class that takes its table, when a body or method index is out of
    // range, or when two classes have the same name.
    //
    // The program points into the packages (`source`): the packages must
    // outlive the program.
    static ScriptProgram link(const std::vector<const ScriptPackage*>& packages,
                              const std::vector<std::string>& native_types);

    const std::vector<ScriptProgramClass>& classes() const { return classes_; }
    const std::vector<ScriptProgramMethod>& methods() const { return methods_; }
    const std::vector<ScriptProgramBody>& bodies() const { return bodies_; }
    // The global name table: each name of a reference of kind 0 or 1, in the
    // order it first appears.
    const std::vector<std::string>& names() const { return names_; }
    // The value types: the value classes in class order, then the native
    // types that have no class.
    const std::vector<std::string>& value_types() const { return value_types_; }

    // The id + 1 of the class named `name`, 0 when there is none.
    std::uint32_t find_class(const std::string& name) const;

private:
    std::uint32_t require_class(const std::string& name) const;
    std::uint32_t name_index(const std::string& name);
    std::uint32_t value_type(const std::string& name) const;
    void link_class(std::uint32_t id, const std::vector<bool>& linked);

    std::vector<ScriptProgramClass> classes_;
    std::vector<ScriptProgramMethod> methods_;
    std::vector<ScriptProgramBody> bodies_;
    std::vector<std::string> names_;
    std::vector<std::string> value_types_;
    std::map<std::string, std::uint32_t> class_ids_;
    std::map<std::string, std::uint32_t> name_ids_;
};

}  // namespace bo2::engine
