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

// A creature argument is a value of the class: its cell holds the address of
// the slot and the slot holds the creature.
std::uint32_t creature_argument(const ScriptMachine& machine, ScriptAddress args, std::size_t index) {
    const ScriptAddress slot =
        machine.memory().u32(args + static_cast<ScriptAddress>(index * 4));
    return machine.memory().u32(slot);
}

void store_byte(ScriptMachine& machine, ScriptAddress result, bool value) {
    if (result != 0) {
        machine.memory().set_u8(result, value ? 1 : 0);
    }
}

void update_flags(ScriptMachine& machine, ScriptObjectId self, std::uint32_t and_mask,
                  std::uint32_t or_mask) {
    ScriptCreatureHost& host = host_of(machine);
    const std::uint32_t creature = machine.creature_of(self);
    host.set_creature_flags(creature, (host.creature_flags(creature) & and_mask) | or_mask);
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

// FUN_00767170 (native), FUN_0075fa70 (helper): the three floats of the
// creature's position into the block of the result.
void native_get_position(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                         ScriptAddress, ScriptAddress result) {
    const ScriptAddress block = result == 0 ? 0 : machine.memory().u32(result);
    if (block == 0) {
        return;
    }
    const std::array<float, 3> position = host_of(machine).creature_position(machine.creature_of(self));
    for (std::size_t index = 0; index < 3; ++index) {
        machine.memory().set_f32(block + static_cast<ScriptAddress>(index * 4), position[index]);
    }
}

// FUN_00767180 (native), FUN_0075faa0 (helper): the same with the rotation.
void native_get_rotation(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                         ScriptAddress, ScriptAddress result) {
    const ScriptAddress block = result == 0 ? 0 : machine.memory().u32(result);
    if (block == 0) {
        return;
    }
    const std::array<float, 3> rotation = host_of(machine).creature_rotation(machine.creature_of(self));
    for (std::size_t index = 0; index < 3; ++index) {
        machine.memory().set_f32(block + static_cast<ScriptAddress>(index * 4), rotation[index]);
    }
}

// FUN_00767360 (native), FUN_00760870 (helper): an empty name is 0 without a
// search and never logs.
void native_does_named_creature_exist(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                      ScriptAddress args, ScriptAddress result) {
    if (result == 0) {
        return;
    }
    const std::string name = name_argument(machine, args);
    if (name.empty()) {
        store_byte(machine, result, false);
        return;
    }
    store_byte(machine, result,
               host_of(machine).find_creature(machine.creature_of(self), name) != 0);
}

// FUN_00767400 (native), FUN_007603e0 (helper): a creature with a package is
// hibernated, the others are removed.
void native_kill_creature(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                          ScriptAddress) {
    const std::uint32_t creature = creature_argument(machine, args, 0);
    if (creature == 0) {
        return;
    }
    ScriptCreatureHost& host = host_of(machine);
    if (host.creature_has_package(creature)) {
        host.hibernate_creature(creature);
    } else {
        host.remove_creature(creature);
    }
}

// FUN_00767420 (native), FUN_00760410 (helper): hibernate the creature.
void native_hibernate_creature(ScriptMachine& machine, ScriptObjectId, std::uint32_t,
                               ScriptAddress args, ScriptAddress) {
    const std::uint32_t creature = creature_argument(machine, args, 0);
    if (creature != 0) {
        host_of(machine).hibernate_creature(creature);
    }
}

// FUN_00767440 (native), FUN_00760430 (helper): wake the creature.
void native_unhibernate_creature(ScriptMachine& machine, ScriptObjectId, std::uint32_t,
                                 ScriptAddress args, ScriptAddress) {
    const std::uint32_t creature = creature_argument(machine, args, 0);
    if (creature != 0) {
        host_of(machine).unhibernate_creature(creature);
    }
}

// FUN_00767630 (native), FUN_00760450 (helper): the object of the creature,
// whatever its class.
void native_get_script(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                       ScriptAddress result) {
    const std::uint32_t creature = creature_argument(machine, args, 0);
    const ScriptObjectId object = creature == 0 ? 0 : host_of(machine).creature_object(creature);
    machine.store_handle(result, object == 0 ? 0 : machine.handle_of(object));
}

// FUN_00767320 (native), FUN_00760810 (helper): whether the creature is not 0.
void native_creature_valid(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                           ScriptAddress result) {
    store_byte(machine, result, creature_argument(machine, args, 0) != 0);
}

// FUN_00767340 (native), FUN_00760850 (helper): whether the two creatures are equal.
void native_creature_equal(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                           ScriptAddress result) {
    store_byte(machine, result,
               creature_argument(machine, args, 0) == creature_argument(machine, args, 1));
}

// FUN_00767f80 (native), FUN_00760df0 (helper): the subtraction is unsigned, so with frame 0 the previous one
// is 0xffffffff and the creature was not drawn in it.
void native_was_drawn_last_frame(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                 ScriptAddress, ScriptAddress result) {
    ScriptCreatureHost& host = host_of(machine);
    const std::uint32_t creature = machine.creature_of(self);
    const std::uint32_t frame = host.frame();
    store_byte(machine, result, !(host.drawn_frame(creature) < frame - 1U));
}

// FUN_007686a0 and FUN_007686b0 (natives), FUN_00762480 and FUN_00762420 (helpers): out of the two update groups.
void native_disable_simulate(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                             ScriptAddress, ScriptAddress) {
    host_of(machine).leave_group(machine.creature_of(self), ScriptCreatureHost::Group::process);
}

void native_disable_eot_simulate(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                 ScriptAddress, ScriptAddress) {
    host_of(machine).leave_group(machine.creature_of(self), ScriptCreatureHost::Group::second);
}

// FUN_007686c0 and FUN_007686d0 (natives), FUN_00762440 and FUN_00762450 (helpers): the level-of-detail switch of the models.
void native_enable_model_lod(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress,
                             ScriptAddress) {
    host_of(machine).set_model_lod(true);
}

void native_disable_model_lod(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress,
                              ScriptAddress) {
    host_of(machine).set_model_lod(false);
}

// FUN_007686e0 (native), FUN_00762460 (helper): clears bit 0x10.
void native_disable_edit_mode_simulate(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                       ScriptAddress, ScriptAddress) {
    update_flags(machine, self, ~0x10U, 0U);
}

// FUN_007686f0 (native), FUN_00762470 (helper): sets bit 0x800.
void native_enable_sim_time_accumulate_while_culled(ScriptMachine& machine, ScriptObjectId self,
                                                    std::uint32_t, ScriptAddress, ScriptAddress) {
    update_flags(machine, self, ~0U, 0x800U);
}

// FUN_00768700 (native), FUN_007624a0 (helper): sets bit 4.
void native_disable_sim_cull(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                             ScriptAddress, ScriptAddress) {
    update_flags(machine, self, ~0U, 4U);
}

// FUN_00768710 (native), FUN_007624b0 (helper): clears bit 4 only when bit 8 is clear.
void native_enable_sim_cull(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                            ScriptAddress, ScriptAddress) {
    ScriptCreatureHost& host = host_of(machine);
    const std::uint32_t creature = machine.creature_of(self);
    const std::uint32_t flags = host.creature_flags(creature);
    if ((flags & 8U) == 0U) {
        host.set_creature_flags(creature, flags & ~4U);
    }
}

// FUN_00768720 (native), FUN_007624d0 (helper): sets bit 0x10000.
void native_disable_sim_when_script_done(ScriptMachine& machine, ScriptObjectId self,
                                         std::uint32_t, ScriptAddress, ScriptAddress) {
    update_flags(machine, self, ~0U, 0x10000U);
}

// FUN_007621a0: the name is the string of the first argument cell, empty when
// that cell holds no string; the host queues the region and nothing is
// written. The original skips the call when its debug option (5, 0x21) is on,
// which the port never sets.
void native_load_region(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                        ScriptAddress) {
    const std::string name = machine.string_at(machine.memory().u32(args));
    host_of(machine).load_region(name);
}

void bind_creature_natives(ScriptMachine& machine) {
    machine.bind_native("creaturescript", 1, native_get_position);
    machine.bind_native("creaturescript", 2, native_get_rotation);
    machine.bind_native("creaturescript", 10, native_does_named_creature_exist);
    machine.bind_native("creaturescript", 11, native_kill_creature);
    machine.bind_native("creaturescript", 12, native_hibernate_creature);
    machine.bind_native("creaturescript", 13, native_unhibernate_creature);
    machine.bind_native("creaturescript", 16, native_get_script);
    machine.bind_native("creaturescript", 8, native_creature_valid);
    machine.bind_native("creaturescript", 9, native_creature_equal);
    machine.bind_native("creaturescript", 47, native_was_drawn_last_frame);
    machine.bind_native("creaturescript", 69, native_disable_simulate);
    machine.bind_native("creaturescript", 70, native_disable_eot_simulate);
    machine.bind_native("creaturescript", 71, native_enable_model_lod);
    machine.bind_native("creaturescript", 72, native_disable_model_lod);
    machine.bind_native("creaturescript", 73, native_disable_edit_mode_simulate);
    machine.bind_native("creaturescript", 74, native_enable_sim_time_accumulate_while_culled);
    machine.bind_native("creaturescript", 75, native_disable_sim_cull);
    machine.bind_native("creaturescript", 76, native_enable_sim_cull);
    machine.bind_native("creaturescript", 77, native_disable_sim_when_script_done);
    machine.bind_native("creaturescript", 78, native_load_region);
    machine.bind_native("creaturescript", 6, native_creature_by_name);
    machine.bind_native("creaturescript", 7, native_creature_self);
    machine.bind_native("bo2creaturescript", 0, native_get_kain);
    machine.bind_native("bo2creaturescript", 1, native_creature_script_by_name);
    machine.bind_native("bo2creaturescript", 11, native_get_hud);
}

}  // namespace bo2::engine
