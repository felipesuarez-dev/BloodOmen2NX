#include "bo2/engine/script_program.hpp"

#include <algorithm>

#include "bo2/engine/script_memory.hpp"

namespace bo2::engine {

namespace {

constexpr std::uint16_t kValueClassKind = 5;
constexpr std::uint32_t kPushNameCell = 0x04000000U;  // reference kind 0
constexpr std::uint32_t kNameWord = 0x5F000000U;      // reference kind 1

}  // namespace

std::uint32_t ScriptProgram::find_class(const std::string& name) const {
    const auto it = class_ids_.find(name);
    return it == class_ids_.end() ? 0 : it->second + 1;
}

std::uint32_t ScriptProgram::require_class(const std::string& name) const {
    const auto it = class_ids_.find(name);
    if (it == class_ids_.end()) {
        throw ScriptError("script class " + name + " does not exist");
    }
    return it->second;
}

std::uint32_t ScriptProgram::name_index(const std::string& name) {
    const auto [it, added] =
        name_ids_.emplace(name, static_cast<std::uint32_t>(names_.size()));
    if (added) {
        names_.push_back(name);
    }
    return it->second;
}

std::uint32_t ScriptProgram::value_type(const std::string& name) const {
    const auto it = std::find(value_types_.begin(), value_types_.end(), name);
    return it == value_types_.end()
               ? 0
               : static_cast<std::uint32_t>(it - value_types_.begin()) + 1;
}

// The link record of one class (FUN_0058c060): parent, slot tables and the
// code words of its references.
void ScriptProgram::link_class(std::uint32_t id, const std::vector<bool>& linked) {
    const ScriptClass& cls = *classes_[id].source;
    if (!cls.parent.empty()) {
        classes_[id].parent = require_class(cls.parent) + 1;
    }

    // One table per state: an own table of slot_count empty slots
    // (FUN_0057a7a0) or a copy of the table of the same state of the owner
    // (FUN_0057a800).
    std::vector<std::vector<std::uint32_t>> states;
    states.reserve(cls.states.size());
    for (std::size_t state = 0; state < cls.states.size(); ++state) {
        const std::string& owner = cls.state_owners[state];
        if (owner.empty()) {
            states.emplace_back(cls.slot_count, 0);
            continue;
        }
        const std::uint32_t owner_id = require_class(owner);
        if (!linked[owner_id]) {
            throw ScriptError("script class " + owner + " is linked after " + cls.name +
                              ", which takes its state table");
        }
        const ScriptProgramClass& source = classes_[owner_id];
        if (state >= source.states.size()) {
            throw ScriptError("script class " + owner + " has no state table " +
                              std::to_string(state));
        }
        states.push_back(source.states[state]);
    }

    // Each slot entry writes the body of the class it names (FUN_0057a890).
    for (const ScriptSlot& entry : cls.slots) {
        const ScriptProgramClass& target = classes_[require_class(entry.class_name)];
        if (entry.body >= target.bodies.size()) {
            throw ScriptError("script slot entry of " + cls.name + " names a body out of range");
        }
        std::vector<std::uint32_t>& table = states[entry.state];
        if (entry.slot >= table.size()) {
            throw ScriptError("script slot entry of " + cls.name + " is past its state table");
        }
        table[entry.slot] = target.bodies[entry.body] + 1;
    }
    classes_[id].states = std::move(states);

    // The words that the file leaves to the loader (docs/SCRIPTS.md,
    // "Loading and linking").
    for (std::size_t body = 0; body < cls.references.size(); ++body) {
        std::vector<std::uint32_t>& code = bodies_[classes_[id].bodies[body]].code;
        for (const ScriptReference& reference : cls.references[body]) {
            switch (reference.kind) {
            case 0:
                code[reference.word] = kPushNameCell | name_index(reference.name);
                break;
            case 1:
                code[reference.word] = kNameWord | name_index(reference.name);
                break;
            case 2:
                code[reference.word] = value_type(reference.name);
                break;
            case 3: {
                const ScriptProgramClass& target = classes_[require_class(reference.name)];
                if (reference.index >= target.methods.size()) {
                    throw ScriptError("script reference names a method out of range");
                }
                code[reference.word] = target.methods[reference.index] + 1;
                break;
            }
            case 4: {
                const ScriptProgramClass& target = classes_[require_class(reference.name)];
                if (reference.index >= target.bodies.size()) {
                    throw ScriptError("script reference names a body out of range");
                }
                code[reference.word] = target.bodies[reference.index] + 1;
                break;
            }
            case 5: {
                const std::uint32_t tables = require_class(reference.name);
                const ScriptProgramClass& target = classes_[require_class(reference.second)];
                if (reference.index >= target.methods.size()) {
                    throw ScriptError("script reference names a method out of range");
                }
                code[reference.word] = tables + 1;
                code[reference.word + 1U] = target.methods[reference.index] + 1;
                break;
            }
            default:
                throw ScriptError("script reference kind is unknown");
            }
        }
    }
}

ScriptProgram ScriptProgram::link(const std::vector<const ScriptPackage*>& packages,
                                  const std::vector<std::string>& native_types) {
    ScriptProgram program;
    // Ids for every class, method and body first, so that a reference may name
    // a class declared after its own.
    for (const ScriptPackage* package : packages) {
        for (const ScriptClass& cls : package->classes) {
            const auto id = static_cast<std::uint32_t>(program.classes_.size());
            if (!program.class_ids_.emplace(cls.name, id).second) {
                throw ScriptError("two script classes are named " + cls.name);
            }
            ScriptProgramClass linked;
            linked.name = cls.name;
            linked.source = &cls;
            linked.value_class = cls.kind == kValueClassKind;
            for (std::size_t index = 0; index < cls.methods.size(); ++index) {
                linked.methods.push_back(static_cast<std::uint32_t>(program.methods_.size()));
                program.methods_.push_back(
                    {id, static_cast<std::uint16_t>(index), &cls.methods[index]});
            }
            for (std::size_t index = 0; index < cls.bodies.size(); ++index) {
                linked.bodies.push_back(static_cast<std::uint32_t>(program.bodies_.size()));
                program.bodies_.push_back({id, static_cast<std::uint16_t>(index),
                                           &cls.bodies[index], cls.bodies[index].code});
            }
            if (linked.value_class) {
                program.value_types_.push_back(cls.name);
            }
            program.classes_.push_back(std::move(linked));
        }
    }
    for (const std::string& name : native_types) {
        if (program.value_type(name) == 0) {
            program.value_types_.push_back(name);
        }
    }
    std::vector<bool> linked(program.classes_.size(), false);
    for (std::uint32_t id = 0; id < program.classes_.size(); ++id) {
        program.link_class(id, linked);
        linked[id] = true;
    }
    return program;
}

}  // namespace bo2::engine
