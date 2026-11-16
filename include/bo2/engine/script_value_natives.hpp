#pragma once

#include "bo2/engine/script_machine.hpp"

namespace bo2::engine {

// The natives of the five value classes the machine stores (docs/SCRIPTS.md,
// "The natives of the value classes"). `self` is the address of the slot that
// holds the block (or the waypoints list) address, not an object.

// position, bodies 0 to 6 (FUN_0076d420 to FUN_0076d4e0).
void position_getx(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                   ScriptAddress args, ScriptAddress result);
void position_gety(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                   ScriptAddress args, ScriptAddress result);
void position_getz(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                   ScriptAddress args, ScriptAddress result);
void position_setx(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                   ScriptAddress args, ScriptAddress result);
void position_sety(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                   ScriptAddress args, ScriptAddress result);
void position_setz(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                   ScriptAddress args, ScriptAddress result);
void position_setvalue(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                       ScriptAddress args, ScriptAddress result);

// rotation, bodies 0 to 7 (FUN_0076d190 to FUN_0076d270).
void rotation_getx(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                   ScriptAddress args, ScriptAddress result);
void rotation_gety(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                   ScriptAddress args, ScriptAddress result);
void rotation_getz(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                   ScriptAddress args, ScriptAddress result);
void rotation_setx(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                   ScriptAddress args, ScriptAddress result);
void rotation_sety(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                   ScriptAddress args, ScriptAddress result);
void rotation_setz(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                   ScriptAddress args, ScriptAddress result);
void rotation_setvalue(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                       ScriptAddress args, ScriptAddress result);
void rotation_rotatez(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                      ScriptAddress args, ScriptAddress result);

// color, bodies 0 to 4 (FUN_0076cf30 to FUN_0076cfc0).
void color_setvalue(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                    ScriptAddress args, ScriptAddress result);
void color_setred(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                  ScriptAddress args, ScriptAddress result);
void color_setgreen(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                    ScriptAddress args, ScriptAddress result);
void color_setblue(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                   ScriptAddress args, ScriptAddress result);
void color_setalpha(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                    ScriptAddress args, ScriptAddress result);

// gridposition, bodies 0 to 4 (FUN_0076b760 to FUN_0076b7e0).
void gridposition_getx(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                       ScriptAddress args, ScriptAddress result);
void gridposition_gety(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                       ScriptAddress args, ScriptAddress result);
void gridposition_setx(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                       ScriptAddress args, ScriptAddress result);
void gridposition_sety(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                       ScriptAddress args, ScriptAddress result);
void gridposition_setvalue(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                           ScriptAddress args, ScriptAddress result);

// waypoints, bodies 0 and 1 (FUN_0076d630, FUN_0076d650).
void waypoints_count(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                     ScriptAddress args, ScriptAddress result);
void waypoints_getwaypoint(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                           ScriptAddress args, ScriptAddress result);

// Binds the 27 by class and body index. A class the program does not have is
// skipped (the region packages carry only the value classes they use).
void bind_value_natives(ScriptMachine& machine);

}  // namespace bo2::engine
