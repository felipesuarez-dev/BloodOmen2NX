#pragma once

#include "bo2/engine/script_machine.hpp"

namespace bo2::engine {

// The natives of effect with an empty slot (docs/SCRIPTS.md, "The natives of
// sound, soundgroup and effect"). The port has no effect system, so the value
// slots are always 0; a non-zero slot throws ScriptError. `self` is the
// address of the slot, not an object.

// effect, bodies 0 to 6: the movers and the trackers (FUN_0076cc40 to
// FUN_0076ccf0; helpers FUN_00515220, FUN_00515260, FUN_00515420,
// FUN_00515610, FUN_00515640, FUN_00515670 and FUN_005155e0).
void native_effect_move(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                        ScriptAddress args, ScriptAddress result);
void native_effect_move_relative(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                 ScriptAddress args, ScriptAddress result);
void native_effect_move_relative_to_creature(ScriptMachine& machine, ScriptObjectId self,
                                             std::uint32_t argc, ScriptAddress args,
                                             ScriptAddress result);
void native_effect_trigger_event(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                 ScriptAddress args, ScriptAddress result);
void native_effect_set_attractor(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                 ScriptAddress args, ScriptAddress result);
void native_effect_set_attractor_track_creature(ScriptMachine& machine, ScriptObjectId self,
                                                std::uint32_t argc, ScriptAddress args,
                                                ScriptAddress result);
void native_effect_set_track_bone(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                  ScriptAddress args, ScriptAddress result);

// effect, bodies 7 to 11 and 13: start, stop, reset, isrunning, forceupdate
// and clone (FUN_0076cd10 to FUN_0076cda0; helpers FUN_00515730, FUN_005157b0,
// FUN_005157d0, FUN_005157e0, FUN_00515780 and FUN_005158b0).
void native_effect_start(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                         ScriptAddress args, ScriptAddress result);
void native_effect_stop(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                        ScriptAddress args, ScriptAddress result);
void native_effect_reset(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                         ScriptAddress args, ScriptAddress result);
void native_effect_is_running(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                              ScriptAddress args, ScriptAddress result);
void native_effect_force_update(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                ScriptAddress args, ScriptAddress result);
void native_effect_clone(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                         ScriptAddress args, ScriptAddress result);

// effect, body 12: play, a task (FUN_0076cd80, FUN_00515800).
std::unique_ptr<ScriptTask> native_effect_play(ScriptMachine& machine, ScriptObjectId self,
                                               ScriptObjectId owner, std::uint32_t argc,
                                               ScriptAddress args);

// Binds the 14 by body index in effect; a program without the class is not an
// error.
void bind_effect_natives(ScriptMachine& machine);

}  // namespace bo2::engine
