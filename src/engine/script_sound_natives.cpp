#include "bo2/engine/script_sound_natives.hpp"

#include <string>

namespace bo2::engine {

namespace {

constexpr std::uint32_t kOneFloat = 0x3F800000U;  // 1.0 (FUN_0079b7e0)

ScriptAddress block_of(const ScriptMachine& machine, ScriptAddress slot) {
    return slot == 0 ? 0 : machine.memory().u32(slot);
}

// The port has no sound system: a slot with an instance cannot be handled.
void require_empty(const ScriptMachine& machine, ScriptAddress slot, const char* class_name,
                   const char* method) {
    if (block_of(machine, slot) != 0) {
        throw ScriptError(std::string(class_name) + "." + method + " needs the " + class_name +
                          " system, which is not ported");
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
    require_empty(machine, self, "soundgroup", "setmute");
}

// FUN_0076ba80 (native), FUN_0079b480 (helper): setpause.
void native_soundgroup_set_pause(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                 ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "soundgroup", "setpause");
}

// FUN_0076baa0 (native), FUN_0079b4a0 and FUN_0079b560 (helpers): setvolume.
void native_soundgroup_set_volume(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                  ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "soundgroup", "setvolume");
}

// FUN_0076bac0 (native), FUN_0079b4d0 and FUN_0079b5f0 (helpers): setpan.
void native_soundgroup_set_pan(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                               ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "soundgroup", "setpan");
}

// FUN_0076bae0 (native), FUN_0079b500 and FUN_0079b680 (helpers): setpitch.
void native_soundgroup_set_pitch(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                 ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "soundgroup", "setpitch");
}

// FUN_0076bb00 (native), FUN_0079b530 and FUN_0079b710 (helpers): setmaxdist.
void native_soundgroup_set_max_dist(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                    ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "soundgroup", "setmaxdist");
}

// FUN_0076bb20 (native), FUN_0079b560 (helper): setvolumewait; no task.
std::unique_ptr<ScriptTask> native_soundgroup_set_volume_wait(ScriptMachine& machine,
                                                              ScriptObjectId self, ScriptObjectId,
                                                              std::uint32_t, ScriptAddress) {
    require_empty(machine, self, "soundgroup", "setvolumewait");
    return nullptr;
}

// FUN_0076bb40 (native), FUN_0079b5f0 (helper): setpanwait; no task.
std::unique_ptr<ScriptTask> native_soundgroup_set_pan_wait(ScriptMachine& machine, ScriptObjectId self,
                                                           ScriptObjectId, std::uint32_t,
                                                           ScriptAddress) {
    require_empty(machine, self, "soundgroup", "setpanwait");
    return nullptr;
}

// FUN_0076bb60 (native), FUN_0079b680 (helper): setpitchwait; no task.
std::unique_ptr<ScriptTask> native_soundgroup_set_pitch_wait(ScriptMachine& machine,
                                                             ScriptObjectId self, ScriptObjectId,
                                                             std::uint32_t, ScriptAddress) {
    require_empty(machine, self, "soundgroup", "setpitchwait");
    return nullptr;
}

// FUN_0076bb80 (native), FUN_0079b710 (helper): setmaxdistwait; no task.
std::unique_ptr<ScriptTask> native_soundgroup_set_max_dist_wait(ScriptMachine& machine,
                                                                ScriptObjectId self, ScriptObjectId,
                                                                std::uint32_t, ScriptAddress) {
    require_empty(machine, self, "soundgroup", "setmaxdistwait");
    return nullptr;
}

// FUN_0076bba0 (native), FUN_0079b7a0 (helper): ismuted.
void native_soundgroup_is_muted(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                ScriptAddress, ScriptAddress result) {
    require_empty(machine, self, "soundgroup", "ismuted");
    store_byte(machine, result, 0);
}

// FUN_0076bbc0 (native), FUN_0079b7c0 (helper): ispaused.
void native_soundgroup_is_paused(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                 ScriptAddress, ScriptAddress result) {
    require_empty(machine, self, "soundgroup", "ispaused");
    store_byte(machine, result, 0);
}

// FUN_0076bbe0 (native), FUN_0079b7e0 (helper): getvolume is 1.0.
void native_soundgroup_get_volume(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                  ScriptAddress, ScriptAddress result) {
    require_empty(machine, self, "soundgroup", "getvolume");
    store_word(machine, result, kOneFloat);
}

// FUN_0076bc00 (native), FUN_0079b800 (helper): getpan is 0.
void native_soundgroup_get_pan(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                               ScriptAddress, ScriptAddress result) {
    require_empty(machine, self, "soundgroup", "getpan");
    store_word(machine, result, 0);
}

// FUN_0076bc20 (native), FUN_0079b820 (helper): getpitch is 0.
void native_soundgroup_get_pitch(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                 ScriptAddress, ScriptAddress result) {
    require_empty(machine, self, "soundgroup", "getpitch");
    store_word(machine, result, 0);
}

// FUN_0076bc40 (native), FUN_0079b840 (helper): getmaxdist is 0.
void native_soundgroup_get_max_dist(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                    ScriptAddress, ScriptAddress result) {
    require_empty(machine, self, "soundgroup", "getmaxdist");
    store_word(machine, result, 0);
}

// FUN_0076bf00 (native), FUN_0079c200 (helper): moverelative.
void native_sound_move_relative(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "sound", "moverelative");
}

// FUN_0076bf20 (native), FUN_0079c220 (helper): startsound.
void native_sound_start(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                        ScriptAddress) {
    require_empty(machine, self, "sound", "startsound");
}

// FUN_0076bf30 (native), FUN_0079c350 (helper): stop.
void native_sound_stop(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                       ScriptAddress) {
    require_empty(machine, self, "sound", "stop");
}

// FUN_0076bf60 (native), FUN_0079c3a0 (helper): spawnsound.
void native_sound_spawn(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                        ScriptAddress) {
    require_empty(machine, self, "sound", "spawnsound");
}

// FUN_0076bf70 (native), FUN_0079c2b0 (helper): startlipsound.
void native_sound_start_lip(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                            ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "sound", "startlipsound");
}

// FUN_0076bf90 (native), FUN_0079c440 (helper): play; no task.
std::unique_ptr<ScriptTask> native_sound_play(ScriptMachine& machine, ScriptObjectId self,
                                              ScriptObjectId, std::uint32_t, ScriptAddress) {
    require_empty(machine, self, "sound", "play");
    return nullptr;
}

// FUN_0076bfb0 (native), FUN_0079c460 and FUN_0079c520 (helpers): setvolume.
void native_sound_set_volume(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                             ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "sound", "setvolume");
}

// FUN_0076bfd0 (native), FUN_0079c490 and FUN_0079c5c0 (helpers): setpan.
void native_sound_set_pan(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                          ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "sound", "setpan");
}

// FUN_0076bff0 (native), FUN_0079c4c0 and FUN_0079c660 (helpers): setpitch.
void native_sound_set_pitch(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                            ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "sound", "setpitch");
}

// FUN_0076c010 (native), FUN_0079c4f0 and FUN_0079c700 (helpers): setmaxdist.
void native_sound_set_max_dist(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                               ScriptAddress, ScriptAddress) {
    require_empty(machine, self, "sound", "setmaxdist");
}

// FUN_0076c030 (native), FUN_0079c520 (helper): setvolumewait; no task.
std::unique_ptr<ScriptTask> native_sound_set_volume_wait(ScriptMachine& machine, ScriptObjectId self,
                                                         ScriptObjectId, std::uint32_t,
                                                         ScriptAddress) {
    require_empty(machine, self, "sound", "setvolumewait");
    return nullptr;
}

// FUN_0076c050 (native), FUN_0079c5c0 (helper): setpanwait; no task.
std::unique_ptr<ScriptTask> native_sound_set_pan_wait(ScriptMachine& machine, ScriptObjectId self,
                                                      ScriptObjectId, std::uint32_t,
                                                      ScriptAddress) {
    require_empty(machine, self, "sound", "setpanwait");
    return nullptr;
}

// FUN_0076c070 (native), FUN_0079c660 (helper): setpitchwait; no task.
std::unique_ptr<ScriptTask> native_sound_set_pitch_wait(ScriptMachine& machine, ScriptObjectId self,
                                                        ScriptObjectId, std::uint32_t,
                                                        ScriptAddress) {
    require_empty(machine, self, "sound", "setpitchwait");
    return nullptr;
}

// FUN_0076c090 (native), FUN_0079c700 (helper): setmaxdistwait; no task.
std::unique_ptr<ScriptTask> native_sound_set_max_dist_wait(ScriptMachine& machine,
                                                           ScriptObjectId self, ScriptObjectId,
                                                           std::uint32_t, ScriptAddress) {
    require_empty(machine, self, "sound", "setmaxdistwait");
    return nullptr;
}

// FUN_0076bf40 (native), FUN_0079c360 (helper): isrunning.
void native_sound_is_running(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                             ScriptAddress, ScriptAddress result) {
    require_empty(machine, self, "sound", "isrunning");
    store_byte(machine, result, 0);
}

// FUN_0076c0b0 (native), FUN_0079c7a0 (helper): getvolume is 1.0.
void native_sound_get_volume(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                             ScriptAddress, ScriptAddress result) {
    require_empty(machine, self, "sound", "getvolume");
    store_word(machine, result, kOneFloat);
}

// FUN_0076c0d0 (native), FUN_0079c7d0 (helper): getpan is 0.
void native_sound_get_pan(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                          ScriptAddress result) {
    require_empty(machine, self, "sound", "getpan");
    store_word(machine, result, 0);
}

// FUN_0076c0f0 (native), FUN_0079c800 (helper): getpitch is 0.
void native_sound_get_pitch(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                            ScriptAddress, ScriptAddress result) {
    require_empty(machine, self, "sound", "getpitch");
    store_word(machine, result, 0);
}

// FUN_0076c110 (native), FUN_0079c830 (helper): getmaxdist is 0.
void native_sound_get_max_dist(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                               ScriptAddress, ScriptAddress result) {
    require_empty(machine, self, "sound", "getmaxdist");
    store_word(machine, result, 0);
}

void bind_sound_natives(ScriptMachine& machine) {
    if (machine.program().find_class("sound") == 0) {
        return;
    }
    machine.bind_native("sound", 0, native_sound_move_relative);
    machine.bind_native("sound", 1, native_sound_start);
    machine.bind_native("sound", 2, native_sound_stop);
    machine.bind_native("sound", 4, native_sound_spawn);
    machine.bind_native("sound", 5, native_sound_start_lip);
    machine.bind_task_native("sound", 6, native_sound_play);
    machine.bind_native("sound", 7, native_sound_set_volume);
    machine.bind_native("sound", 8, native_sound_set_pan);
    machine.bind_native("sound", 9, native_sound_set_pitch);
    machine.bind_native("sound", 10, native_sound_set_max_dist);
    machine.bind_task_native("sound", 11, native_sound_set_volume_wait);
    machine.bind_task_native("sound", 12, native_sound_set_pan_wait);
    machine.bind_task_native("sound", 13, native_sound_set_pitch_wait);
    machine.bind_task_native("sound", 14, native_sound_set_max_dist_wait);
    machine.bind_native("sound", 3, native_sound_is_running);
    machine.bind_native("sound", 15, native_sound_get_volume);
    machine.bind_native("sound", 16, native_sound_get_pan);
    machine.bind_native("sound", 17, native_sound_get_pitch);
    machine.bind_native("sound", 18, native_sound_get_max_dist);
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
