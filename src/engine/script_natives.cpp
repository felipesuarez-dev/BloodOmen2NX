#include "bo2/engine/script_natives.hpp"

#include <cmath>
#include <cstdio>

namespace bo2::engine {

namespace {

constexpr float kDegToRad = 0.0174532924f;  // 0x3c8efa35 (FUN_00798e60, FUN_007990d0)
constexpr float kRadToDeg = 57.2957802f;    // 0x42652ee1 (FUN_007990f0)
constexpr double kTwoPowMinus32 = 1.0 / 4294967296.0;  // 0x2f800000

// The original has one generator at 0xa74c00. ScriptNative has no state, so
// the binder keeps the generator here; one per process, as in the original.
GameRandom* g_random = nullptr;

GameRandom& random_source() {
    if (g_random == nullptr) {
        throw ScriptError("a basescript random native ran with no generator");
    }
    return *g_random;
}

float float_argument(const ScriptMachine& machine, ScriptAddress args, std::size_t index) {
    return machine.memory().f32(args + static_cast<ScriptAddress>(index * 4));
}

std::uint32_t word_argument(const ScriptMachine& machine, ScriptAddress args, std::size_t index) {
    return machine.memory().u32(args + static_cast<ScriptAddress>(index * 4));
}

ScriptAddress block_of(const ScriptMachine& machine, ScriptAddress slot) {
    return slot == 0 ? 0 : machine.memory().u32(slot);
}

void store_word(ScriptMachine& machine, ScriptAddress result, std::uint32_t value) {
    if (result != 0) {
        machine.memory().set_u32(result, value);
    }
}

void store_float(ScriptMachine& machine, ScriptAddress result, float value) {
    if (result != 0) {
        machine.memory().set_f32(result, value);
    }
}

void store_byte(ScriptMachine& machine, ScriptAddress result, bool value) {
    if (result != 0) {
        machine.memory().set_u8(result, value ? 1 : 0);
    }
}

std::string string_argument(const ScriptMachine& machine, ScriptAddress args) {
    return machine.string_at(word_argument(machine, args, 0));
}

}  // namespace

// FUN_007644a0, FUN_00798b30: the result takes the handle of self, counted.
void native_self(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                 ScriptAddress result) {
    if (result != 0) {
        machine.store_handle(result, machine.handle_of(self));
    }
}

// FUN_00764550, FUN_00798e20: the result byte is the state of self.
void native_get_state(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                      ScriptAddress result) {
    store_word(machine, result, machine.state_of(self));
}

// FUN_00764560, FUN_00798b40: the result byte is 1 when the argument is 0.
void native_is_null(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                    ScriptAddress result) {
    store_byte(machine, result, word_argument(machine, args, 0) == 0);
}

// FUN_00764580, FUN_00798b60: the result byte is 1 when the two arguments are equal.
void native_is_equal(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                     ScriptAddress result) {
    store_byte(machine, result, word_argument(machine, args, 0) == word_argument(machine, args, 1));
}

// FUN_007645c0, FUN_00798c90: the result byte is 1 when self has a main thread.
void native_is_running(ScriptMachine& machine, ScriptObjectId self, std::uint32_t, ScriptAddress,
                       ScriptAddress result) {
    store_byte(machine, result, machine.main_thread(self) != nullptr);
}

// FUN_007645e0, FUN_00798e30: the three arguments into the position block of the result.
void native_position(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                     ScriptAddress result) {
    const ScriptAddress block = block_of(machine, result);
    if (block == 0) {
        return;
    }
    for (std::size_t index = 0; index < 3; ++index) {
        machine.memory().set_f32(block + static_cast<ScriptAddress>(index * 4),
                                 float_argument(machine, args, index));
    }
}

// FUN_00764600, FUN_00798e60: each argument times 0x3c8efa35 into the rotation block.
void native_rotation(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                     ScriptAddress result) {
    const ScriptAddress block = block_of(machine, result);
    if (block == 0) {
        return;
    }
    for (std::size_t index = 0; index < 3; ++index) {
        machine.memory().set_f32(block + static_cast<ScriptAddress>(index * 4),
                                 float_argument(machine, args, index) * kDegToRad);
    }
}

// FUN_00764620, FUN_00798eb0: the four arguments into the colour block of the result.
void native_color(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                  ScriptAddress result) {
    const ScriptAddress block = block_of(machine, result);
    if (block == 0) {
        return;
    }
    for (std::size_t index = 0; index < 4; ++index) {
        machine.memory().set_f32(block + static_cast<ScriptAddress>(index * 4),
                                 float_argument(machine, args, index));
    }
}

// FUN_00764650, FUN_00798f00: the string to the log.
void native_print_string(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                         ScriptAddress) {
    machine.warn(string_argument(machine, args));
}

// FUN_007646f0, FUN_00798f30: the number as %f to the log.
void native_print_number(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                         ScriptAddress) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%f", static_cast<double>(float_argument(machine, args, 0)));
    machine.warn(buffer);
}

// FUN_00764710, FUN_00798f80: the string to the log.
void native_log_string(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                       ScriptAddress) {
    machine.warn(string_argument(machine, args));
}

// FUN_007647b0, FUN_00798fa0: nothing, as in the original.
void native_log_number(ScriptMachine&, ScriptObjectId, std::uint32_t, ScriptAddress, ScriptAddress) {
    // FUN_00798fa0: nothing.
}

// FUN_007647d0, FUN_00798fb0: (b - a) * r * 2^-32 + a, in double.
void native_random(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                   ScriptAddress result) {
    // FUN_00798fb0: (b - a) * r * 2^-32 + a, in double, stored as a float.
    const double a = float_argument(machine, args, 0);
    const double b = float_argument(machine, args, 1);
    const std::uint32_t r = random_source().next();
    const double value = (b - a) * static_cast<double>(r) * kTwoPowMinus32 + a;
    store_float(machine, result, static_cast<float>(value));
}

// FUN_007647f0, FUN_00798ff0: b when r * 2^-32 is 1.0, else (int)((b - a + 1) * f) + a.
void native_random_int(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                       ScriptAddress result) {
    // FUN_00798ff0: f = r * 2^-32; b when f is 1.0, else (int)((b - a + 1) * f) + a.
    const std::int32_t a = static_cast<std::int32_t>(word_argument(machine, args, 0));
    const std::int32_t b = static_cast<std::int32_t>(word_argument(machine, args, 1));
    const std::uint32_t r = random_source().next();
    const double fraction = static_cast<double>(r) * kTwoPowMinus32;
    std::int32_t value = b;
    if (fraction != 1.0) {
        value = static_cast<std::int32_t>(static_cast<double>(b - a + 1) * fraction) + a;
    }
    store_word(machine, result, static_cast<std::uint32_t>(value));
}

// FUN_00764810, FUN_00799060: the distance between two position slots.
void native_distance(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                     ScriptAddress result) {
    // FUN_00799060: each argument is the address of a position slot; the sum
    // is dy*dy + dz*dz + dx*dx in floats, and 0 when a block is missing.
    const ScriptAddress first = block_of(machine, word_argument(machine, args, 0));
    const ScriptAddress second = block_of(machine, word_argument(machine, args, 1));
    float value = 0.0f;
    if (first != 0 && second != 0) {
        const float dx = machine.memory().f32(first) - machine.memory().f32(second);
        const float dy = machine.memory().f32(first + 4) - machine.memory().f32(second + 4);
        const float dz = machine.memory().f32(first + 8) - machine.memory().f32(second + 8);
        const float sum = dy * dy + dz * dz + dx * dx;
        if (sum != 0.0f) {
            value = std::sqrt(sum);
        }
    }
    store_float(machine, result, value);
}

// FUN_00764830, FUN_007990d0: the argument times 0x3c8efa35.
void native_deg_to_rad(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                       ScriptAddress result) {
    store_float(machine, result, float_argument(machine, args, 0) * kDegToRad);
}

// FUN_00764850, FUN_007990f0: the argument times 0x42652ee1.
void native_rad_to_deg(ScriptMachine& machine, ScriptObjectId, std::uint32_t, ScriptAddress args,
                       ScriptAddress result) {
    store_float(machine, result, float_argument(machine, args, 0) * kRadToDeg);
}

void bind_basescript_natives(ScriptMachine& machine, GameRandom& random) {
    g_random = &random;
    machine.bind_native("basescript", 0, native_self);
    machine.bind_native("basescript", 1, native_get_state);
    machine.bind_native("basescript", 2, native_is_null);
    machine.bind_native("basescript", 3, native_is_equal);
    machine.bind_native("basescript", 5, native_is_running);
    machine.bind_native("basescript", 7, native_position);
    machine.bind_native("basescript", 8, native_rotation);
    machine.bind_native("basescript", 9, native_color);
    machine.bind_native("basescript", 10, native_print_string);
    machine.bind_native("basescript", 11, native_print_number);
    machine.bind_native("basescript", 12, native_log_string);
    machine.bind_native("basescript", 13, native_log_number);
    machine.bind_native("basescript", 15, native_random);
    machine.bind_native("basescript", 16, native_random_int);
    machine.bind_native("basescript", 17, native_distance);
    machine.bind_native("basescript", 18, native_deg_to_rad);
    machine.bind_native("basescript", 19, native_rad_to_deg);
}

}  // namespace bo2::engine
