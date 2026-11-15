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

// Binds the five to their bodies (the binders of the two classes start at
// 0x7668f0 and 0x4cd3f0).
void bind_creature_natives(ScriptMachine& machine);

}  // namespace bo2::engine
