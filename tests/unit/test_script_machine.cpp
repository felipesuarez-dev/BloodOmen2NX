#include <cstdint>
#include <functional>
#include <iostream>

#include "bo2/engine/script_memory.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

bool throws(const std::function<void()>& action) {
    try {
        action();
    } catch (const bo2::engine::ScriptError&) {
        return true;
    }
    return false;
}

using bo2::engine::ScriptMemory;

// docs/ARCHITECTURE.md, "Script machine": nothing starts below 8, blocks are
// aligned to 4 and zeroed, and a reservation of 0 takes 4 bytes.
void test_memory_first_blocks() {
    ScriptMemory memory;
    CHECK(memory.allocate(10) == 8);
    CHECK(memory.allocate(1) == 20);
    CHECK(memory.allocate(0) == 24);
    CHECK(memory.live_blocks() == 3);
    CHECK(memory.block_size(8) == 10);
    CHECK(memory.block_size(20) == 1);
    CHECK(memory.block_size(24) == 4);
    for (std::uint32_t address = 8; address < 18; ++address) {
        CHECK(memory.u8(address) == 0);
    }
    CHECK(memory.u8(20) == 0);
    CHECK(memory.u32(24) == 0);
}

void test_memory_little_endian() {
    ScriptMemory memory;
    const std::uint32_t block = memory.allocate(8);
    memory.set_u32(block, 0x11223344U);
    CHECK(memory.u8(block) == 0x44);
    CHECK(memory.u8(block + 3) == 0x11);
    CHECK(memory.u32(block) == 0x11223344U);
    memory.set_u8(block + 4, 0x7F);
    CHECK(memory.u32(block + 4) == 0x7FU);
    memory.set_f32(block + 4, 1.5f);
    CHECK(memory.f32(block + 4) == 1.5f);
    CHECK(memory.u32(block + 4) == 0x3FC00000U);
}

void test_memory_bounds() {
    ScriptMemory memory;
    CHECK(memory.allocate(10) == 8);
    CHECK(memory.allocate(8) == 20);
    CHECK(throws([&] { static_cast<void>(memory.u32(4)); }));   // below the first block
    CHECK(throws([&] { static_cast<void>(memory.u32(16)); }));  // ends past the block of 10
    CHECK(throws([&] { static_cast<void>(memory.u32(18)); }));  // in the padding
    CHECK(throws([&] { static_cast<void>(memory.u8(0)); }));    // null
    CHECK(throws([&] { static_cast<void>(memory.u8(28)); }));   // past the arena
    CHECK(throws([&] { memory.set_u32(17, 1); }));
    CHECK(throws([&] { memory.copy(20, 14, 8); }));             // source crosses two blocks
    CHECK(throws([&] { memory.copy(14, 20, 8); }));             // target crosses two blocks
    CHECK(!throws([&] { static_cast<void>(memory.u32(14)); }));
    CHECK(throws([&] { static_cast<void>(memory.allocate(0xFFFFFFF0U)); }));
    CHECK(memory.live_blocks() == 2);
}

void test_memory_release() {
    ScriptMemory memory;
    const std::uint32_t first = memory.allocate(8);
    const std::uint32_t second = memory.allocate(8);
    memory.set_u32(first, 5);
    memory.release(first);
    CHECK(memory.live_blocks() == 1);
    CHECK(throws([&] { static_cast<void>(memory.u32(first)); }));
    CHECK(throws([&] { memory.release(first); }));       // released twice
    CHECK(throws([&] { memory.release(second + 1); }));  // not the start of a block
    CHECK(throws([&] { static_cast<void>(memory.block_size(first)); }));
    CHECK(memory.u32(second) == 0);
    // A reused block starts zeroed again.
    const std::uint32_t again = memory.allocate(4);
    CHECK(memory.u32(again) == 0);
}

void test_memory_copy() {
    ScriptMemory memory;
    const std::uint32_t from = memory.allocate(7);
    const std::uint32_t to = memory.allocate(9);
    for (std::uint32_t index = 0; index < 7; ++index) {
        memory.set_u8(from + index, static_cast<std::uint8_t>(index + 1));
    }
    memory.copy(to + 2, from, 7);
    CHECK(memory.u8(to) == 0);
    CHECK(memory.u8(to + 1) == 0);
    for (std::uint32_t index = 0; index < 7; ++index) {
        CHECK(memory.u8(to + 2 + index) == index + 1);
    }
    // Overlapping ranges inside one block.
    memory.copy(to, to + 2, 7);
    CHECK(memory.u8(to) == 1);
    CHECK(memory.u8(to + 6) == 7);
}

}  // namespace

int main() {
    test_memory_first_blocks();
    test_memory_little_endian();
    test_memory_bounds();
    test_memory_release();
    test_memory_copy();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "script machine tests passed\n";
    return 0;
}
