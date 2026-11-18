#include "bo2/engine/script_effect_natives.hpp"

#include <string>

namespace bo2::engine {

namespace {

ScriptAddress block_of(const ScriptMachine& machine, ScriptAddress slot) {
    return slot == 0 ? 0 : machine.memory().u32(slot);
}

// The port has no effect system: a slot with an instance cannot be handled.
void require_empty(const ScriptMachine& machine, ScriptAddress slot, const char* method) {
    if (block_of(machine, slot) != 0) {
        throw ScriptError(std::string("effect.") + method +
                          " needs the effect system, which is not ported");
    }
}

void store_byte(ScriptMachine& machine, ScriptAddress result, std::uint8_t value) {
    if (result != 0) {
        machine.memory().set_u8(result, value);
    }
}

// FUN_005158b0: the effect in the slot loses one count; at 0 the block is
// freed. The slot is left at 0.
void release_effect(ScriptMachine& machine, ScriptAddress slot) {
    if (slot == 0) {
        return;
    }
    const ScriptAddress block = machine.memory().u32(slot);
    if (block == 0) {
        return;
    }
    const std::uint32_t count = machine.memory().u32(block + 4);
    if (count > 1) {
        machine.memory().set_u32(block + 4, count - 1);
    } else {
        machine.memory().release(block);
    }
    machine.memory().set_u32(slot, 0);
}

}  // namespace

// FUN_0076cc40 (native), FUN_00515220 (helper): move.
void native_effect_move(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                        ScriptAddress) {
    require_empty(machine, self, "move");
}

// FUN_0076cc60 (native), FUN_00515260 (helper): moverelative.
void native_effect_move_relative(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                 ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "moverelative");
}

// FUN_0076cc80 (native), FUN_00515420 (helper): moverelativetocreature.
void native_effect_move_relative_to_creature(ScriptMachine& machine, ScriptObjectId self,
                                             std::uint32_t, ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "moverelativetocreature");
}

// FUN_0076cca0 (native), FUN_00515610 (helper): triggerevent.
void native_effect_trigger_event(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                 ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "triggerevent");
}

// FUN_0076ccb0 (native), FUN_00515640 (helper): setattractor.
void native_effect_set_attractor(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                 ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "setattractor");
}

// FUN_0076ccd0 (native), FUN_00515670 (helper): setattractortrackcreature.
void native_effect_set_attractor_track_creature(ScriptMachine& machine, ScriptObjectId self,
                                                std::uint32_t, ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "setattractortrackcreature");
}

// FUN_0076ccf0 (native), FUN_005155e0 (helper): settrackbone.
void native_effect_set_track_bone(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                  ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "settrackbone");
}

// FUN_0076cd10 (native), FUN_00515730 (helper): starteffect.
void native_effect_start(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                         ScriptAddress) {
    require_empty(machine, self, "starteffect");
}

// FUN_0076cd30 (native), FUN_005157b0 (helper): stop.
void native_effect_stop(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                        ScriptAddress) {
    require_empty(machine, self, "stop");
}

// FUN_0076cd40 (native), FUN_005157d0 (helper): reset.
void native_effect_reset(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                         ScriptAddress) {
    require_empty(machine, self, "reset");
}

// FUN_0076cd50 (native), FUN_005157e0 (helper): isrunning.
void native_effect_is_running(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                              ScriptAddress, ScriptAddress result) {
    require_empty(machine, self, "isrunning");
    store_byte(machine, result, 0);
}

// FUN_0076cd70 (native), FUN_00515780 (helper): forceupdate.
void native_effect_force_update(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "forceupdate");
}

// FUN_0076cd80 (native), FUN_00515800 (helper): play; no task.
std::unique_ptr<ScriptTask> native_effect_play(ScriptMachine& machine, ScriptObjectId self,
                                               ScriptObjectId, std::uint32_t, ScriptAddress) {
    require_empty(machine, self, "play");
    return nullptr;
}

// FUN_0076cda0 (native), FUN_005158b0 (helper): clone lets go of the effect
// in the result slot first, then looks at the empty self.
void native_effect_clone(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                         ScriptAddress result) {
    release_effect(machine, result);
    require_empty(machine, self, "clone");
}

void bind_effect_natives(ScriptMachine& machine) {
    if (machine.program().find_class("effect") == 0) {
        return;
    }
    machine.bind_native("effect", 0, native_effect_move);
    machine.bind_native("effect", 1, native_effect_move_relative);
    machine.bind_native("effect", 2, native_effect_move_relative_to_creature);
    machine.bind_native("effect", 3, native_effect_trigger_event);
    machine.bind_native("effect", 4, native_effect_set_attractor);
    machine.bind_native("effect", 5, native_effect_set_attractor_track_creature);
    machine.bind_native("effect", 6, native_effect_set_track_bone);
    machine.bind_native("effect", 7, native_effect_start);
    machine.bind_native("effect", 8, native_effect_stop);
    machine.bind_native("effect", 9, native_effect_reset);
    machine.bind_native("effect", 10, native_effect_is_running);
    machine.bind_native("effect", 11, native_effect_force_update);
    machine.bind_task_native("effect", 12, native_effect_play);
    machine.bind_native("effect", 13, native_effect_clone);
}

}  // namespace bo2::engine
