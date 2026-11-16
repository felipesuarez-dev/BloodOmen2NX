#include "bo2/engine/script_value_natives.hpp"

#include <cstddef>
#include <initializer_list>

namespace bo2::engine {

namespace {

constexpr float kDegToRad = 0.0174532924f;  // 0x3c8efa35 (FUN_0079e200, FUN_0079e2a0)
constexpr float kRadToDeg = 57.2957802f;    // 0x42652ee1 (FUN_0079e180)

ScriptAddress block_of(const ScriptMachine& machine, ScriptAddress slot) {
    return slot == 0 ? 0 : machine.memory().u32(slot);
}

float float_at(const ScriptMachine& machine, ScriptAddress block, std::uint32_t offset) {
    return block == 0 ? 0.0f : machine.memory().f32(block + offset);
}

std::uint32_t word_at(const ScriptMachine& machine, ScriptAddress block, std::uint32_t offset) {
    return block == 0 ? 0 : machine.memory().u32(block + offset);
}

float float_argument(const ScriptMachine& machine, ScriptAddress args, std::size_t index) {
    return machine.memory().f32(args + static_cast<ScriptAddress>(index * 4));
}

std::uint32_t word_argument(const ScriptMachine& machine, ScriptAddress args, std::size_t index) {
    return machine.memory().u32(args + static_cast<ScriptAddress>(index * 4));
}

void store_float(ScriptMachine& machine, ScriptAddress result, float value) {
    if (result != 0) {
        machine.memory().set_f32(result, value);
    }
}

void store_word(ScriptMachine& machine, ScriptAddress result, std::uint32_t value) {
    if (result != 0) {
        machine.memory().set_u32(result, value);
    }
}

}  // namespace

// position, bodies 0 to 6. FUN_0079e4a0, FUN_0079e4c0, FUN_0079e4e0,
// FUN_0079e500, FUN_0079e520, FUN_0079e540 and FUN_0079e560.
// FUN_0076d420 (native), FUN_0079e4a0 (helper): the float at +0.
void position_getx(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                   ScriptAddress result) {
    store_float(machine, result, float_at(machine, block_of(machine, self), 0));
}

// FUN_0076d440 (native), FUN_0079e4c0 (helper): the float at +4.
void position_gety(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                   ScriptAddress result) {
    store_float(machine, result, float_at(machine, block_of(machine, self), 4));
}

// FUN_0076d460 (native), FUN_0079e4e0 (helper): the float at +8.
void position_getz(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                   ScriptAddress result) {
    store_float(machine, result, float_at(machine, block_of(machine, self), 8));
}

// FUN_0076d480 (native), FUN_0079e500 (helper): writes the argument at +0.
void position_setx(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                   ScriptAddress) {
    const ScriptAddress block = block_of(machine, self);
    if (block != 0) {
        machine.memory().set_f32(block, float_argument(machine, args, 0));
    }
}

// FUN_0076d4a0 (native), FUN_0079e520 (helper): writes the argument at +4.
void position_sety(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                   ScriptAddress) {
    const ScriptAddress block = block_of(machine, self);
    if (block != 0) {
        machine.memory().set_f32(block + 4, float_argument(machine, args, 0));
    }
}

// FUN_0076d4c0 (native), FUN_0079e540 (helper): writes the argument at +8.
void position_setz(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                   ScriptAddress) {
    const ScriptAddress block = block_of(machine, self);
    if (block != 0) {
        machine.memory().set_f32(block + 8, float_argument(machine, args, 0));
    }
}

// FUN_0076d4e0 (native), FUN_0079e560 (helper): writes the three arguments.
void position_setvalue(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                       ScriptAddress) {
    const ScriptAddress block = block_of(machine, self);
    if (block == 0) {
        return;
    }
    for (std::size_t index = 0; index < 3; ++index) {
        machine.memory().set_f32(block + static_cast<ScriptAddress>(index * 4),
                                 float_argument(machine, args, index));
    }
}

// rotation, bodies 0 to 7. The block holds radians; the natives speak degrees.
// FUN_0079e180, FUN_0079e1a0, FUN_0079e1d0, FUN_0079e200, FUN_0079e220,
// FUN_0079e240, FUN_0079e260 and FUN_0079e2a0.
// FUN_0076d190 (native), FUN_0079e180 (helper): the angle in degrees, 57.2957802 times +0.
void rotation_getx(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                   ScriptAddress result) {
    store_float(machine, result, float_at(machine, block_of(machine, self), 0) * kRadToDeg);
}

// FUN_0076d1b0 (native), FUN_0079e1a0 (helper): the angle in degrees, 57.2957802 times +4.
void rotation_gety(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                   ScriptAddress result) {
    store_float(machine, result, float_at(machine, block_of(machine, self), 4) * kRadToDeg);
}

// FUN_0076d1d0 (native), FUN_0079e1d0 (helper): the angle in degrees, 57.2957802 times +8.
void rotation_getz(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                   ScriptAddress result) {
    store_float(machine, result, float_at(machine, block_of(machine, self), 8) * kRadToDeg);
}

// FUN_0076d1f0 (native), FUN_0079e200 (helper): 0.0174532924 times the argument at +0.
void rotation_setx(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                   ScriptAddress) {
    const ScriptAddress block = block_of(machine, self);
    if (block != 0) {
        machine.memory().set_f32(block, float_argument(machine, args, 0) * kDegToRad);
    }
}

// FUN_0076d210 (native), FUN_0079e220 (helper): 0.0174532924 times the argument at +4.
void rotation_sety(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                   ScriptAddress) {
    const ScriptAddress block = block_of(machine, self);
    if (block != 0) {
        machine.memory().set_f32(block + 4, float_argument(machine, args, 0) * kDegToRad);
    }
}

// FUN_0076d230 (native), FUN_0079e240 (helper): 0.0174532924 times the argument at +8.
void rotation_setz(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                   ScriptAddress) {
    const ScriptAddress block = block_of(machine, self);
    if (block != 0) {
        machine.memory().set_f32(block + 8, float_argument(machine, args, 0) * kDegToRad);
    }
}

// FUN_0076d250 (native), FUN_0079e260 (helper): 0.0174532924 times each of the three arguments.
void rotation_setvalue(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                       ScriptAddress) {
    const ScriptAddress block = block_of(machine, self);
    if (block == 0) {
        return;
    }
    for (std::size_t index = 0; index < 3; ++index) {
        machine.memory().set_f32(block + static_cast<ScriptAddress>(index * 4),
                                 float_argument(machine, args, index) * kDegToRad);
    }
}

// FUN_0076d270 (native), FUN_0079e2a0 (helper): +8 += 0.0174532924 times the argument.
void rotation_rotatez(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                      ScriptAddress) {
    const ScriptAddress block = block_of(machine, self);
    if (block != 0) {
        machine.memory().set_f32(block + 8,
                                 machine.memory().f32(block + 8) +
                                     float_argument(machine, args, 0) * kDegToRad);
    }
}

// color, bodies 0 to 4. FUN_0079dee0, FUN_0079df10, FUN_0079df30, FUN_0079df50
// and FUN_0079df70.
// FUN_0076cf30 (native), FUN_0079dee0 (helper): writes the four arguments.
void color_setvalue(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                    ScriptAddress) {
    const ScriptAddress block = block_of(machine, self);
    if (block == 0) {
        return;
    }
    for (std::size_t index = 0; index < 4; ++index) {
        machine.memory().set_f32(block + static_cast<ScriptAddress>(index * 4),
                                 float_argument(machine, args, index));
    }
}

// FUN_0076cf60 (native), FUN_0079df10 (helper): writes the argument at +0.
void color_setred(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                  ScriptAddress) {
    const ScriptAddress block = block_of(machine, self);
    if (block != 0) {
        machine.memory().set_f32(block, float_argument(machine, args, 0));
    }
}

// FUN_0076cf80 (native), FUN_0079df30 (helper): writes the argument at +4.
void color_setgreen(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                    ScriptAddress) {
    const ScriptAddress block = block_of(machine, self);
    if (block != 0) {
        machine.memory().set_f32(block + 4, float_argument(machine, args, 0));
    }
}

// FUN_0076cfa0 (native), FUN_0079df50 (helper): writes the argument at +8.
void color_setblue(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                   ScriptAddress) {
    const ScriptAddress block = block_of(machine, self);
    if (block != 0) {
        machine.memory().set_f32(block + 8, float_argument(machine, args, 0));
    }
}

// FUN_0076cfc0 (native), FUN_0079df70 (helper): writes the argument at +0xc.
void color_setalpha(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                    ScriptAddress) {
    const ScriptAddress block = block_of(machine, self);
    if (block != 0) {
        machine.memory().set_f32(block + 12, float_argument(machine, args, 0));
    }
}

// gridposition, bodies 0 to 4. FUN_0079b130, FUN_0079b150, FUN_0079b170,
// FUN_0079b190 and FUN_0079b1b0.
// FUN_0076b760 (native), FUN_0079b130 (helper): the word at +0.
void gridposition_getx(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                       ScriptAddress result) {
    store_word(machine, result, word_at(machine, block_of(machine, self), 0));
}

// FUN_0076b780 (native), FUN_0079b150 (helper): the word at +4.
void gridposition_gety(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                       ScriptAddress result) {
    store_word(machine, result, word_at(machine, block_of(machine, self), 4));
}

// FUN_0076b7a0 (native), FUN_0079b170 (helper): writes the argument at +0.
void gridposition_setx(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                       ScriptAddress) {
    const ScriptAddress block = block_of(machine, self);
    if (block != 0) {
        machine.memory().set_u32(block, word_argument(machine, args, 0));
    }
}

// FUN_0076b7c0 (native), FUN_0079b190 (helper): writes the argument at +4.
void gridposition_sety(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress args,
                       ScriptAddress) {
    const ScriptAddress block = block_of(machine, self);
    if (block != 0) {
        machine.memory().set_u32(block + 4, word_argument(machine, args, 0));
    }
}

// FUN_0076b7e0 (native), FUN_0079b1b0 (helper): writes the two arguments.
void gridposition_setvalue(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                           ScriptAddress args, ScriptAddress) {
    const ScriptAddress block = block_of(machine, self);
    if (block != 0) {
        machine.memory().set_u32(block, word_argument(machine, args, 0));
        machine.memory().set_u32(block + 4, word_argument(machine, args, 1));
    }
}

// waypoints, bodies 0 and 1. FUN_0079e710 and FUN_0079e730.
// FUN_0076d630 (native), FUN_0079e710 (helper): the count of the list, 0 when the slot holds none.
void waypoints_count(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                     ScriptAddress result) {
    const ScriptAddress list = block_of(machine, self);
    store_word(machine, result, list == 0 ? 0 : machine.memory().u32(list));
}

// FUN_0076d650 (native), FUN_0079e730 (helper): point i into the position block of the result, or three zeros.
void waypoints_getwaypoint(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                           ScriptAddress args, ScriptAddress result) {
    const ScriptAddress target = block_of(machine, result);
    if (target == 0) {
        return;  // the original would fail
    }
    const ScriptAddress list = block_of(machine, self);
    const std::int32_t index = static_cast<std::int32_t>(word_argument(machine, args, 0));
    if (list != 0) {
        const std::uint32_t count = machine.memory().u32(list);
        if (index >= 0 && static_cast<std::uint32_t>(index) < count) {
            const ScriptAddress points = machine.memory().u32(list + 4);
            machine.memory().copy(target, points + static_cast<ScriptAddress>(index) * 12U, 12);
            return;
        }
    }
    for (std::size_t component = 0; component < 3; ++component) {
        machine.memory().set_f32(target + static_cast<ScriptAddress>(component * 4), 0.0f);
    }
}

void bind_value_natives(ScriptMachine& machine) {
    const auto bind = [&](const char* name, std::initializer_list<ScriptNative> natives) {
        if (machine.program().find_class(name) == 0) {
            return;
        }
        std::uint16_t body = 0;
        for (const ScriptNative native : natives) {
            machine.bind_native(name, body, native);
            ++body;
        }
    };
    bind("position", {position_getx, position_gety, position_getz, position_setx, position_sety,
                      position_setz, position_setvalue});
    bind("rotation", {rotation_getx, rotation_gety, rotation_getz, rotation_setx, rotation_sety,
                      rotation_setz, rotation_setvalue, rotation_rotatez});
    bind("color", {color_setvalue, color_setred, color_setgreen, color_setblue, color_setalpha});
    bind("gridposition", {gridposition_getx, gridposition_gety, gridposition_setx, gridposition_sety,
                          gridposition_setvalue});
    bind("waypoints", {waypoints_count, waypoints_getwaypoint});
}

}  // namespace bo2::engine
