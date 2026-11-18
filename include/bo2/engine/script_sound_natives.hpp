#pragma once

#include "bo2/engine/script_machine.hpp"

namespace bo2::engine {

// The natives of soundgroup and sound with an empty slot (docs/SCRIPTS.md,
// "The natives of sound, soundgroup and effect"). The port has no sound
// system, so the value slots are always 0; a non-zero slot throws
// ScriptError. `self` is the address of the slot, not an object.

// soundgroup, bodies 0 to 5: the setters. Their helpers are
// FUN_0079b460, FUN_0079b480, FUN_0079b4a0, FUN_0079b4d0, FUN_0079b500 and
// FUN_0079b530.
void native_soundgroup_set_mute(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                ScriptAddress args, ScriptAddress result);
void native_soundgroup_set_pause(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                 ScriptAddress args, ScriptAddress result);
void native_soundgroup_set_volume(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                  ScriptAddress args, ScriptAddress result);
void native_soundgroup_set_pan(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                               ScriptAddress args, ScriptAddress result);
void native_soundgroup_set_pitch(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                 ScriptAddress args, ScriptAddress result);
void native_soundgroup_set_max_dist(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                    ScriptAddress args, ScriptAddress result);

// soundgroup, bodies 6 to 9: the task setters (FUN_0076bb20 to FUN_0076bb80,
// helpers FUN_0079b560, FUN_0079b5f0, FUN_0079b680 and FUN_0079b710).
std::unique_ptr<ScriptTask> native_soundgroup_set_volume_wait(ScriptMachine& machine,
                                                              ScriptObjectId self,
                                                              ScriptObjectId owner,
                                                              std::uint32_t argc,
                                                              ScriptAddress args);
std::unique_ptr<ScriptTask> native_soundgroup_set_pan_wait(ScriptMachine& machine,
                                                           ScriptObjectId self, ScriptObjectId owner,
                                                           std::uint32_t argc, ScriptAddress args);
std::unique_ptr<ScriptTask> native_soundgroup_set_pitch_wait(ScriptMachine& machine,
                                                             ScriptObjectId self,
                                                             ScriptObjectId owner,
                                                             std::uint32_t argc,
                                                             ScriptAddress args);
std::unique_ptr<ScriptTask> native_soundgroup_set_max_dist_wait(ScriptMachine& machine,
                                                               ScriptObjectId self,
                                                               ScriptObjectId owner,
                                                               std::uint32_t argc,
                                                               ScriptAddress args);

// soundgroup, bodies 10 to 15: the getters (FUN_0076bba0 to FUN_0076bc40,
// helpers FUN_0079b7a0 to FUN_0079b840).
void native_soundgroup_is_muted(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                ScriptAddress args, ScriptAddress result);
void native_soundgroup_is_paused(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                 ScriptAddress args, ScriptAddress result);
void native_soundgroup_get_volume(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                  ScriptAddress args, ScriptAddress result);
void native_soundgroup_get_pan(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                               ScriptAddress args, ScriptAddress result);
void native_soundgroup_get_pitch(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                 ScriptAddress args, ScriptAddress result);
void native_soundgroup_get_max_dist(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                    ScriptAddress args, ScriptAddress result);

// sound, bodies 0 to 5: move, start, stop, spawn and lipsound (FUN_0076bf00
// to FUN_0076bf70; helpers FUN_0079c200, FUN_0079c220, FUN_0079c350,
// FUN_0079c3a0 and FUN_0079c2b0).
void native_sound_move_relative(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                ScriptAddress args, ScriptAddress result);
void native_sound_start(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                        ScriptAddress args, ScriptAddress result);
void native_sound_stop(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                       ScriptAddress args, ScriptAddress result);
void native_sound_spawn(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                        ScriptAddress args, ScriptAddress result);
void native_sound_start_lip(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                            ScriptAddress args, ScriptAddress result);

// sound, body 6: play, a task (FUN_0076bf90, FUN_0079c440).
std::unique_ptr<ScriptTask> native_sound_play(ScriptMachine& machine, ScriptObjectId self,
                                              ScriptObjectId owner, std::uint32_t argc,
                                              ScriptAddress args);

// sound, bodies 7 to 10: the setters (FUN_0076bfb0 to FUN_0076c010; helpers
// FUN_0079c460, FUN_0079c520, FUN_0079c490, FUN_0079c5c0, FUN_0079c4c0,
// FUN_0079c660, FUN_0079c4f0 and FUN_0079c700).
void native_sound_set_volume(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                             ScriptAddress args, ScriptAddress result);
void native_sound_set_pan(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                          ScriptAddress args, ScriptAddress result);
void native_sound_set_pitch(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                            ScriptAddress args, ScriptAddress result);
void native_sound_set_max_dist(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                               ScriptAddress args, ScriptAddress result);

// sound, bodies 11 to 14: the task setters (FUN_0076c030 to FUN_0076c090;
// helpers FUN_0079c520, FUN_0079c5c0, FUN_0079c660 and FUN_0079c700).
std::unique_ptr<ScriptTask> native_sound_set_volume_wait(ScriptMachine& machine, ScriptObjectId self,
                                                         ScriptObjectId owner, std::uint32_t argc,
                                                         ScriptAddress args);
std::unique_ptr<ScriptTask> native_sound_set_pan_wait(ScriptMachine& machine, ScriptObjectId self,
                                                      ScriptObjectId owner, std::uint32_t argc,
                                                      ScriptAddress args);
std::unique_ptr<ScriptTask> native_sound_set_pitch_wait(ScriptMachine& machine, ScriptObjectId self,
                                                        ScriptObjectId owner, std::uint32_t argc,
                                                        ScriptAddress args);
std::unique_ptr<ScriptTask> native_sound_set_max_dist_wait(ScriptMachine& machine,
                                                           ScriptObjectId self, ScriptObjectId owner,
                                                           std::uint32_t argc, ScriptAddress args);

// sound, bodies 15 to 18: isrunning and the getters (FUN_0076bf40 and
// FUN_0076c0b0 to FUN_0076c110; helpers FUN_0079c360, FUN_0079c7a0,
// FUN_0079c7d0, FUN_0079c800 and FUN_0079c830).
void native_sound_is_running(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                             ScriptAddress args, ScriptAddress result);
void native_sound_get_volume(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                             ScriptAddress args, ScriptAddress result);
void native_sound_get_pan(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                          ScriptAddress args, ScriptAddress result);
void native_sound_get_pitch(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                            ScriptAddress args, ScriptAddress result);
void native_sound_get_max_dist(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                               ScriptAddress args, ScriptAddress result);

// Binds the 16 by body index in soundgroup; a program without the class is
// not an error.
void bind_soundgroup_natives(ScriptMachine& machine);

// Binds the 19 by body index in sound; a program without the class is not an
// error.
void bind_sound_natives(ScriptMachine& machine);

}  // namespace bo2::engine
