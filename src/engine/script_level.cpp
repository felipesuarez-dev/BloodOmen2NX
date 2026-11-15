#include "bo2/engine/script_level.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace bo2::engine {

namespace {

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

}  // namespace

ScriptLevel::ScriptLevel(ScriptMachine& machine) : machine_(machine) { machine_.set_creature_host(this); }

ScriptLevel::~ScriptLevel() {
    if (machine_.creature_host() == this) {
        machine_.set_creature_host(nullptr);
    }
}

std::size_t ScriptLevel::add_region(const std::string& region_name, const std::vector<RegionEntityTable>& tables) {
    const std::string package = region_package_name(region_name);
    std::size_t added = 0;
    for (const RegionEntityTable& table : tables) {
        for (const RegionEntity& entity : table.entities) {
            Creature creature;
            creature.name = entity.name;
            creature.package = package;
            creature.script = reserved_script(entity);
            creature.position = {entity.position.x, entity.position.y, entity.position.z};
            add_creature(std::move(creature));
            ++added;
        }
    }
    return added;
}

// FUN_0070e560: the object of the class of the creature's script, when that
// class derives from basecreaturescript (FUN_00580a30); its +0x2c is the
// creature.
std::uint32_t ScriptLevel::add_creature(Creature creature) {
    creature.object = 0;
    creatures_.push_back(std::move(creature));
    const auto word = static_cast<std::uint32_t>(creatures_.size());
    Creature& added = creatures_.back();
    if (!added.script.empty()) {
        const std::uint32_t id = machine_.program().find_class(added.script);
        if (id != 0 && machine_.is_a(id - 1, "basecreaturescript")) {
            try {
                added.object = machine_.create_object(id - 1);
                machine_.set_creature(added.object, word);
            } catch (const ScriptError& error) {
                errors_.push_back(added.name + ": " + error.what());
            }
        }
    }
    active_.push_back(word);
    return word;
}

ScriptLevel::Creature& ScriptLevel::creature(std::uint32_t word) {
    if (word == 0 || word > creatures_.size()) {
        throw ScriptError("no creature " + std::to_string(word));
    }
    return creatures_[word - 1];
}

// FUN_0070e6e0 for an object that hit an error: it stops and the creature
// keeps no object.
void ScriptLevel::stop_object(Creature& creature, const std::string& error) {
    errors_.push_back(creature.name + ": " + error);
    if (creature.object != 0) {
        try {
            machine_.stop(creature.object);
        } catch (const ScriptError&) {
        }
        creature.object = 0;
    }
}

void ScriptLevel::activate() {
    const std::vector<std::uint32_t> order = active_;
    for (const std::uint32_t word : order) {
        Creature& current = creature(word);
        if (current.activated) {
            continue;
        }
        current.activated = true;
        if (current.object == 0) {
            continue;
        }
        try {
            machine_.activate(current.object, [&](const std::string& name) -> ScriptObjectId {
                const std::uint32_t found = find_creature(word, name);
                return found == 0 ? 0 : creature(found).object;
            });
        } catch (const ScriptError& error) {
            stop_object(creature(word), error.what());
        }
    }
}

void ScriptLevel::update(float step) {
    ++frame_;
    if (step > 0.0f) {
        machine_.update_tasks(float_bits(step));  // FUN_00583400, the first time scale is 1
    }
    for (std::uint32_t word = 1; word <= creatures_.size(); ++word) {
        Creature& current = creature(word);
        if (!current.in_process || current.deleted || current.hibernating || (current.flags & 2U) != 0) {
            continue;
        }
        if (!(current.time > 0.0f) || current.object == 0) {
            continue;
        }
        try {
            machine_.run(current.object, float_bits(current.time));
        } catch (const ScriptError& error) {
            stop_object(creature(word), error.what());
        }
    }
    // The end of FUN_00718490: removed creatures go, then those sent to
    // hibernate (FUN_00718970) and those woken (FUN_00718a60).
    for (const std::uint32_t word : std::exchange(to_remove_, {})) {
        Creature& gone = creature(word);
        gone.deleted = true;
        if (gone.object != 0) {
            machine_.stop(gone.object);  // FUN_0070e6e0 in the creature's end
            gone.object = 0;
        }
        active_.erase(std::remove(active_.begin(), active_.end(), word), active_.end());
        hibernating_.erase(std::remove(hibernating_.begin(), hibernating_.end(), word), hibernating_.end());
    }
    for (const std::uint32_t word : std::exchange(to_hibernate_, {})) {
        Creature& sleeper = creature(word);
        if (!sleeper.hibernating && !sleeper.deleted) {
            sleeper.hibernating = true;
            move(active_, hibernating_, word);
        }
    }
    for (const std::uint32_t word : std::exchange(to_wake_, {})) {
        Creature& waking = creature(word);
        if (waking.hibernating && !waking.deleted) {
            waking.hibernating = false;
            move(hibernating_, active_, word);
        }
    }
    // FUN_00718340, every 60 frames.
    if (frame_ % 60 == 0) {
        for (Creature& current : creatures_) {
            if (current.in_process && !current.deleted && (current.flags & 0x10000U) != 0 && current.object != 0 &&
                machine_.main_thread(current.object) == nullptr) {
                current.in_process = false;
            }
        }
    }
}

void ScriptLevel::move(std::vector<std::uint32_t>& from, std::vector<std::uint32_t>& to, std::uint32_t word) {
    from.erase(std::remove(from.begin(), from.end(), word), from.end());
    to.push_back(word);
}

// FUN_00718160: the creature of that name in the package of the owner, or
// one with no package; the active list first, then the hibernating one;
// names compare with case.
std::uint32_t ScriptLevel::find_creature(std::uint32_t owner, const std::string& name) {
    const std::string package = owner == 0 ? std::string() : creature(owner).package;
    for (const std::vector<std::uint32_t>* list : {&active_, &hibernating_}) {
        for (const std::uint32_t word : *list) {
            const Creature& candidate = creature(word);
            if (candidate.name == name && (candidate.package == package || candidate.package.empty())) {
                return word;
            }
        }
    }
    return 0;
}

std::string ScriptLevel::creature_name(std::uint32_t word) { return word == 0 ? std::string() : creature(word).name; }

ScriptObjectId ScriptLevel::creature_object(std::uint32_t word) { return word == 0 ? 0 : creature(word).object; }

// FUN_004a5290: the first creature of the list made by the factory of "kain".
std::uint32_t ScriptLevel::kain() {
    for (const std::uint32_t word : active_) {
        if (creature(word).kain) {
            return word;
        }
    }
    return 0;
}

std::array<float, 3> ScriptLevel::creature_position(std::uint32_t word) { return creature(word).position; }

std::array<float, 3> ScriptLevel::creature_rotation(std::uint32_t word) {
    throw ScriptError("the rotation of creature " + creature(word).name + " (+0xc0) is not read yet");
}

bool ScriptLevel::creature_has_package(std::uint32_t word) { return !creature(word).package.empty(); }

// FUN_00718cf0, FUN_00718d30, FUN_00718b80: the change waits for the end of
// the frame.
void ScriptLevel::hibernate_creature(std::uint32_t word) { to_hibernate_.push_back(word); }

void ScriptLevel::unhibernate_creature(std::uint32_t word) { to_wake_.push_back(word); }

void ScriptLevel::remove_creature(std::uint32_t word) {
    Creature& gone = creature(word);
    gone.flags |= 0x2000U;
    if (!gone.removed) {
        gone.removed = true;
        to_remove_.push_back(word);
    }
}

std::uint32_t ScriptLevel::creature_flags(std::uint32_t word) { return creature(word).flags; }

void ScriptLevel::set_creature_flags(std::uint32_t word, std::uint32_t flags) { creature(word).flags = flags; }

std::uint32_t ScriptLevel::drawn_frame(std::uint32_t word) { return creature(word).drawn; }

void ScriptLevel::leave_group(std::uint32_t word, Group group) {
    if (group == Group::process) {
        creature(word).in_process = false;
    } else {
        creature(word).in_second = false;
    }
}

}  // namespace bo2::engine
