#pragma once

#include "bo2/engine/script_machine.hpp"

namespace bo2::engine {

// The natives of the creature scripts that find creatures and their objects
// (docs/SCRIPTS.md, "The natives that find creatures"). Those that ask the
// game go through the machine's ScriptCreatureHost and throw ScriptError
// when it is not set.

// creaturescript.creaturebyname(string), body 6 (FUN_00767270, FUN_00760750).
void native_creature_by_name(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                             ScriptAddress args, ScriptAddress result);
// creaturescript.creatureself(), body 7 (FUN_00767310, FUN_007607b0).
void native_creature_self(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                          ScriptAddress args, ScriptAddress result);
// bo2creaturescript.getkain(), body 0 (FUN_004cd750, FUN_004d8840).
void native_get_kain(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc, ScriptAddress args,
                     ScriptAddress result);
// bo2creaturescript.creaturescriptbyname(string), body 1 (FUN_004cd760,
// FUN_004d8860).
void native_creature_script_by_name(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                    ScriptAddress args, ScriptAddress result);
// bo2creaturescript.gethud(), body 11 (FUN_004cdf20).
void native_get_hud(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc, ScriptAddress args,
                    ScriptAddress result);

// creaturescript.getposition(), body 1 (FUN_00767170, FUN_0075fa70).
void native_get_position(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                         ScriptAddress args, ScriptAddress result);
// creaturescript.getrotation(), body 2 (FUN_00767180, FUN_0075faa0).
void native_get_rotation(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                         ScriptAddress args, ScriptAddress result);
// creaturescript.doesnamedcreatureexist(string), body 10 (FUN_00767360,
// FUN_00760870).
void native_does_named_creature_exist(ScriptMachine& machine, ScriptObjectId self,
                                      std::uint32_t argc, ScriptAddress args, ScriptAddress result);
// creaturescript.killcreature(creature), body 11 (FUN_00767400, FUN_007603e0).
void native_kill_creature(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                          ScriptAddress args, ScriptAddress result);
// creaturescript.hibernatecreature(creature), body 12 (FUN_00767420, FUN_00760410).
void native_hibernate_creature(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                               ScriptAddress args, ScriptAddress result);
// creaturescript.unhibernatecreature(creature), body 13 (FUN_00767440, FUN_00760430).
void native_unhibernate_creature(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                 ScriptAddress args, ScriptAddress result);
// creaturescript.getscript(creature), body 16 (FUN_00767630, FUN_00760450).
void native_get_script(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                       ScriptAddress args, ScriptAddress result);
// creaturescript.creaturevalid(creature), body 8 (FUN_00767320, FUN_00760810).
void native_creature_valid(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                           ScriptAddress args, ScriptAddress result);
// creaturescript.creatureequal(creature, creature), body 9 (FUN_00767340,
// FUN_00760850).
void native_creature_equal(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                           ScriptAddress args, ScriptAddress result);
// creaturescript.wasdrawnlastframe(), body 47 (FUN_00767f80, FUN_00760df0).
void native_was_drawn_last_frame(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                 ScriptAddress args, ScriptAddress result);
// creaturescript.disablesimulate(), body 69 (FUN_007686a0, FUN_00762480).
void native_disable_simulate(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                             ScriptAddress args, ScriptAddress result);
// creaturescript.disableeotsimulate(), body 70 (FUN_007686b0, FUN_00762420).
void native_disable_eot_simulate(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                 ScriptAddress args, ScriptAddress result);
// creaturescript.enablemodellod(), body 71 (FUN_007686c0, FUN_00762440).
void native_enable_model_lod(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                             ScriptAddress args, ScriptAddress result);
// creaturescript.disablemodellod(), body 72 (FUN_007686d0, FUN_00762450).
void native_disable_model_lod(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                              ScriptAddress args, ScriptAddress result);
// creaturescript.disableeditmodesimulate(), body 73 (FUN_007686e0, FUN_00762460).
void native_disable_edit_mode_simulate(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                                       ScriptAddress args, ScriptAddress result);
// creaturescript.enablesimtimeaccumulatewhileculled(), body 74 (FUN_007686f0,
// FUN_00762470).
void native_enable_sim_time_accumulate_while_culled(ScriptMachine& machine, ScriptObjectId self,
                                                    std::uint32_t argc, ScriptAddress args,
                                                    ScriptAddress result);
// creaturescript.disablesimcull(), body 75 (FUN_00768700, FUN_007624a0).
void native_disable_sim_cull(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                             ScriptAddress args, ScriptAddress result);
// creaturescript.enablesimcull(), body 76 (FUN_00768710, FUN_007624b0).
void native_enable_sim_cull(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                            ScriptAddress args, ScriptAddress result);
// creaturescript.disablesimwhenscriptdone(), body 77 (FUN_00768720, FUN_007624d0).
void native_disable_sim_when_script_done(ScriptMachine& machine, ScriptObjectId self,
                                         std::uint32_t argc, ScriptAddress args,
                                         ScriptAddress result);

// Binds the natives to their bodies (the binders of the two classes start at
// 0x7668f0 and 0x4cd3f0).
void bind_creature_natives(ScriptMachine& machine);

}  // namespace bo2::engine
