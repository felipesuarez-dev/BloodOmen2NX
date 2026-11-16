#pragma once

#include <cstdint>

#include "bo2/engine/random.hpp"
#include "bo2/engine/script_machine.hpp"

namespace bo2::engine {

// The natives of basescript that need no other system (docs/SCRIPTS.md, "The
// natives of basescript"). The log ones write to ScriptMachine::warn, the
// port's log; the random ones use the generator bind_basescript_natives gets.

// body 0 (FUN_007644a0, FUN_00798b30): the result takes the handle of self.
void native_self(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                 ScriptAddress args, ScriptAddress result);
// body 1 (FUN_00764550, FUN_00798e20): the result byte is the state of self.
void native_get_state(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                      ScriptAddress args, ScriptAddress result);
// body 2 (FUN_00764560, FUN_00798b40): the result byte is 1 when the argument is 0.
void native_is_null(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                    ScriptAddress args, ScriptAddress result);
// body 3 (FUN_00764580, FUN_00798b60): the result byte is 1 when the two arguments are equal.
void native_is_equal(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                     ScriptAddress args, ScriptAddress result);
// body 5 (FUN_007645c0, FUN_00798c90): the result byte is 1 when self has a main thread.
void native_is_running(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                       ScriptAddress args, ScriptAddress result);
// body 7 (FUN_007645e0, FUN_00798e30): the three arguments into the position block of the result.
void native_position(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                     ScriptAddress args, ScriptAddress result);
// body 8 (FUN_00764600, FUN_00798e60): each argument times 0x3c8efa35 into the rotation block.
void native_rotation(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                     ScriptAddress args, ScriptAddress result);
// body 9 (FUN_00764620, FUN_00798eb0): the four arguments into the colour block of the result.
void native_color(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                  ScriptAddress args, ScriptAddress result);
// body 10 (FUN_00764650, FUN_00798f00): the string to the log.
void native_print_string(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                         ScriptAddress args, ScriptAddress result);
// body 11 (FUN_007646f0, FUN_00798f30): the number as %f to the log.
void native_print_number(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                         ScriptAddress args, ScriptAddress result);
// body 12 (FUN_00764710, FUN_00798f80): the string to the log.
void native_log_string(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                       ScriptAddress args, ScriptAddress result);
// body 13 (FUN_007647b0, FUN_00798fa0): nothing, as in the original.
void native_log_number(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                       ScriptAddress args, ScriptAddress result);
// body 15 (FUN_007647d0, FUN_00798fb0): (b - a) * r * 2^-32 + a.
void native_random(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                   ScriptAddress args, ScriptAddress result);
// body 16 (FUN_007647f0, FUN_00798ff0): the integer in [a, b] from r * 2^-32.
void native_random_int(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                       ScriptAddress args, ScriptAddress result);
// body 17 (FUN_00764810, FUN_00799060): the distance between two position slots.
void native_distance(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                     ScriptAddress args, ScriptAddress result);
// body 18 (FUN_00764830, FUN_007990d0): the argument times 0x3c8efa35.
void native_deg_to_rad(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                       ScriptAddress args, ScriptAddress result);
// body 19 (FUN_00764850, FUN_007990f0): the argument times 0x42652ee1.
void native_rad_to_deg(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                       ScriptAddress args, ScriptAddress result);

// Binds the 17 to their body indexes in the class `basescript`. The generator
// must outlive the machine; it is kept in a file-local pointer because
// ScriptNative has no state (the original has one global instance at
// 0xa74c00, so a second bind replaces it).
void bind_basescript_natives(ScriptMachine& machine, GameRandom& random);

}  // namespace bo2::engine
