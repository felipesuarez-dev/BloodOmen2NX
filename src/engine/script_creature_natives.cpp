#include "bo2/engine/script_creature_natives.hpp"

#include <string>

namespace bo2::engine {

namespace {

ScriptCreatureHost& host_of(ScriptMachine& machine) {
    ScriptCreatureHost* host = machine.creature_host();
    if (host == nullptr) {
        throw ScriptError("a creature native ran with no creature host");
    }
    return *host;
}

// The name argument; empty when its cell holds no string (FUN_00767270,
// FUN_004cd760).
std::string name_argument(ScriptMachine& machine, ScriptAddress args) {
    return machine.string_at(machine.memory().u32(args));
}

ScriptAddress handle_or_null(ScriptMachine& machine, ScriptObjectId object) {
    return object == 0 ? 0 : machine.handle_of(object);
}

}  // namespace

// FUN_00760750: an empty name gives 0 without a search; a name nobody has
// gives 0 and a log line (0x912b00).
void native_creature_by_name(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                             ScriptAddress result) {
    const std::string name = name_argument(machine, args);
    if (result == 0) {
        return;
    }
    std::uint32_t creature = 0;
    if (!name.empty()) {
        ScriptCreatureHost& host = host_of(machine);
        const std::uint32_t owner = machine.creature_of(self);
        creature = host.find_creature(owner, name);
        if (creature == 0) {
            machine.warn(host.creature_name(owner) + " : GetCreatureWithName('" + name + "') failed");
        }
    }
    machine.memory().set_u32(result, creature);
}

// FUN_007607b0: the creature of the object (+0x2c).
void native_creature_self(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                          ScriptAddress result) {
    if (result != 0) {
        machine.memory().set_u32(result, machine.creature_of(self));
    }
}

// FUN_004d8840: Kain (FUN_004a5290).
void native_get_kain(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress, ScriptAddress result) {
    if (result != 0) {
        machine.memory().set_u32(result, host_of(machine).kain());
    }
}

// FUN_004d8860: the search also runs for an empty name and never logs; the
// object of the creature counts only when it is a creaturescript (the
// dynamic_cast to Screaturescript). FUN_004cd760 stores the handle, 0 too.
void native_creature_script_by_name(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                                    ScriptAddress result) {
    const std::string name = name_argument(machine, args);
    ScriptCreatureHost& host = host_of(machine);
    const std::uint32_t creature = host.find_creature(machine.creature_of(self), name);
    ScriptObjectId object = creature == 0 ? 0 : host.creature_object(creature);
    if (object != 0 && !machine.is_a(machine.class_of(object), "creaturescript")) {
        object = 0;
    }
    machine.store_handle(result, handle_or_null(machine, object));
}

// FUN_004cdf20: the HUD's object (FUN_004d8c60), whatever its class.
void native_get_hud(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress, ScriptAddress result) {
    machine.store_handle(result, handle_or_null(machine, host_of(machine).hud_object()));
}

void bind_creature_natives(ScriptMachine& machine) {
    machine.bind_native("creaturescript", 6, native_creature_by_name);
    machine.bind_native("creaturescript", 7, native_creature_self);
    machine.bind_native("bo2creaturescript", 0, native_get_kain);
    machine.bind_native("bo2creaturescript", 1, native_creature_script_by_name);
    machine.bind_native("bo2creaturescript", 11, native_get_hud);
}

}  // namespace bo2::engine
