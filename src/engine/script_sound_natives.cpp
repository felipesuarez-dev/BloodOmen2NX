#include "bo2/engine/script_sound_natives.hpp"

#include <string>

namespace bo2::engine {

namespace {

constexpr std::uint32_t kOneFloat = 0x3F800000U;  // 1.0 (FUN_0079b7e0)

ScriptAddress block_of(const ScriptMachine& machine, ScriptAddress slot) {
    return slot == 0 ? 0 : machine.memory().u32(slot);
}

// The port has no sound system: a slot with an instance cannot be handled.
void require_empty(const ScriptMachine& machine, ScriptAddress slot, const char* method) {
    if (block_of(machine, slot) != 0) {
        throw ScriptError("soundgroup." + std::string(method) +
                          " needs the soundgroup system, which is not ported");
    }
}

void store_byte(ScriptMachine& machine, ScriptAddress result, std::uint8_t value) {
    if (result != 0) {
        machine.memory().set_u8(result, value);
    }
}

void store_word(ScriptMachine& machine, ScriptAddress result, std::uint32_t value) {
    if (result != 0) {
        machine.memory().set_u32(result, value);
    }
}

}  // namespace

// FUN_0076ba60 (native), FUN_0079b460 (helper): setmute.
void native_soundgroup_set_mute(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "setmute");
}

// FUN_0076ba80 (native), FUN_0079b480 (helper): setpause.
void native_soundgroup_set_pause(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                 ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "setpause");
}

// FUN_0076baa0 (native), FUN_0079b4a0 and FUN_0079b560 (helpers): setvolume.
void native_soundgroup_set_volume(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                  ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "setvolume");
}

// FUN_0076bac0 (native), FUN_0079b4d0 and FUN_0079b5f0 (helpers): setpan.
void native_soundgroup_set_pan(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                               ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "setpan");
}

// FUN_0076bae0 (native), FUN_0079b500 and FUN_0079b680 (helpers): setpitch.
void native_soundgroup_set_pitch(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                 ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "setpitch");
}

// FUN_0076bb00 (native), FUN_0079b530 and FUN_0079b710 (helpers): setmaxdist.
void native_soundgroup_set_max_dist(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                    ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "setmaxdist");
}

// FUN_0076bb20 (native), FUN_0079b560 (helper): setvolumewait; no task.
std::unique_ptr<ScriptTask> native_soundgroup_set_volume_wait(ScriptMachine& machine,
                                                              ScriptObjectId self, ScriptObjectId,
                                                              std::uint32_t, ScriptAddress) {
    require_empty(machine, self, "setvolumewait");
    return nullptr;
}

// FUN_0076bb40 (native), FUN_0079b5f0 (helper): setpanwait; no task.
std::unique_ptr<ScriptTask> native_soundgroup_set_pan_wait(ScriptMachine& machine, ScriptObjectId self,
                                                           ScriptObjectId, std::uint32_t,
                                                           ScriptAddress) {
    require_empty(machine, self, "setpanwait");
    return nullptr;
}

// FUN_0076bb60 (native), FUN_0079b680 (helper): setpitchwait; no task.
std::unique_ptr<ScriptTask> native_soundgroup_set_pitch_wait(ScriptMachine& machine,
                                                             ScriptObjectId self, ScriptObjectId,
                                                             std::uint32_t, ScriptAddress) {
    require_empty(machine, self, "setpitchwait");
    return nullptr;
}

// FUN_0076bb80 (native), FUN_0079b710 (helper): setmaxdistwait; no task.
std::unique_ptr<ScriptTask> native_soundgroup_set_max_dist_wait(ScriptMachine& machine,
                                                                ScriptObjectId self, ScriptObjectId,
                                                                std::uint32_t, ScriptAddress) {
    require_empty(machine, self, "setmaxdistwait");
    return nullptr;
}

// FUN_0076bba0 (native), FUN_0079b7a0 (helper): ismuted.
void native_soundgroup_is_muted(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                ScriptAddress, ScriptAddress result) {
    require_empty(machine, self, "ismuted");
    store_byte(machine, result, 0);
}

// FUN_0076bbc0 (native), FUN_0079b7c0 (helper): ispaused.
void native_soundgroup_is_paused(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                 ScriptAddress, ScriptAddress result) {
    require_empty(machine, self, "ispaused");
    store_byte(machine, result, 0);
}

// FUN_0076bbe0 (native), FUN_0079b7e0 (helper): getvolume is 1.0.
void native_soundgroup_get_volume(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                  ScriptAddress, ScriptAddress result) {
    require_empty(machine, self, "getvolume");
    store_word(machine, result, kOneFloat);
}

// FUN_0076bc00 (native), FUN_0079b800 (helper): getpan is 0.
void native_soundgroup_get_pan(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                               ScriptAddress, ScriptAddress result) {
    require_empty(machine, self, "getpan");
    store_word(machine, result, 0);
}

// FUN_0076bc20 (native), FUN_0079b820 (helper): getpitch is 0.
void native_soundgroup_get_pitch(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                 ScriptAddress, ScriptAddress result) {
    require_empty(machine, self, "getpitch");
    store_word(machine, result, 0);
}

// FUN_0076bc40 (native), FUN_0079b840 (helper): getmaxdist is 0.
void native_soundgroup_get_max_dist(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                    ScriptAddress, ScriptAddress result) {
    require_empty(machine, self, "getmaxdist");
    store_word(machine, result, 0);
}

void bind_soundgroup_natives(ScriptMachine& machine) {
    if (machine.program().find_class("soundgroup") == 0) {
        return;
    }
    machine.bind_native("soundgroup", 0, native_soundgroup_set_mute);
    machine.bind_native("soundgroup", 1, native_soundgroup_set_pause);
    machine.bind_native("soundgroup", 2, native_soundgroup_set_volume);
    machine.bind_native("soundgroup", 3, native_soundgroup_set_pan);
    machine.bind_native("soundgroup", 4, native_soundgroup_set_pitch);
    machine.bind_native("soundgroup", 5, native_soundgroup_set_max_dist);
    machine.bind_task_native("soundgroup", 6, native_soundgroup_set_volume_wait);
    machine.bind_task_native("soundgroup", 7, native_soundgroup_set_pan_wait);
    machine.bind_task_native("soundgroup", 8, native_soundgroup_set_pitch_wait);
    machine.bind_task_native("soundgroup", 9, native_soundgroup_set_max_dist_wait);
    machine.bind_native("soundgroup", 10, native_soundgroup_is_muted);
    machine.bind_native("soundgroup", 11, native_soundgroup_is_paused);
    machine.bind_native("soundgroup", 12, native_soundgroup_get_volume);
    machine.bind_native("soundgroup", 13, native_soundgroup_get_pan);
    machine.bind_native("soundgroup", 14, native_soundgroup_get_pitch);
    machine.bind_native("soundgroup", 15, native_soundgroup_get_max_dist);
}

}  // namespace bo2::engine
