#include "bo2/engine/script_memory.hpp"

#include <algorithm>
#include <cstring>

namespace bo2::engine {

namespace {

constexpr std::uint64_t kFirstBlock = 8;
constexpr std::uint64_t kArenaLimit = 0xFFFFFFF0U;

std::uint64_t round4(std::uint64_t value) {
    return (value + 3) & ~static_cast<std::uint64_t>(3);
}

}  // namespace

ScriptAddress ScriptMemory::allocate(std::uint32_t size) {
    const std::uint32_t stored = size == 0 ? 4 : size;
    const std::uint64_t needed = round4(stored);
    std::uint64_t start = kFirstBlock;
    for (const auto& [block, block_bytes] : blocks_) {
        if (block >= start + needed) {
            break;
        }
        start = std::max(start, round4(static_cast<std::uint64_t>(block) + block_bytes));
    }
    if (start + needed > kArenaLimit) {
        throw ScriptError("script memory is full");
    }
    if (bytes_.size() < start + needed) {
        bytes_.resize(static_cast<std::size_t>(start + needed));
    }
    std::fill_n(bytes_.begin() + static_cast<std::ptrdiff_t>(start),
                static_cast<std::ptrdiff_t>(needed), std::uint8_t{0});
    const ScriptAddress address = static_cast<ScriptAddress>(start);
    blocks_.emplace(address, stored);
    return address;
}

void ScriptMemory::release(ScriptAddress block) {
    if (blocks_.erase(block) == 0) {
        throw ScriptError("script memory release of an address that is not a live block");
    }
}

void ScriptMemory::check(ScriptAddress address, std::uint32_t size) const {
    auto it = blocks_.upper_bound(address);
    if (it == blocks_.begin()) {
        throw ScriptError("script memory access outside a live block");
    }
    --it;
    const std::uint64_t end = static_cast<std::uint64_t>(it->first) + it->second;
    if (static_cast<std::uint64_t>(address) + size > end) {
        throw ScriptError("script memory access outside a live block");
    }
}

std::uint8_t ScriptMemory::u8(ScriptAddress address) const {
    check(address, 1);
    return bytes_[address];
}

std::uint32_t ScriptMemory::u32(ScriptAddress address) const {
    check(address, 4);
    return static_cast<std::uint32_t>(bytes_[address]) |
           (static_cast<std::uint32_t>(bytes_[address + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes_[address + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes_[address + 3]) << 24);
}

float ScriptMemory::f32(ScriptAddress address) const {
    const std::uint32_t bits = u32(address);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

void ScriptMemory::set_u8(ScriptAddress address, std::uint8_t value) {
    check(address, 1);
    bytes_[address] = value;
}

void ScriptMemory::set_u32(ScriptAddress address, std::uint32_t value) {
    check(address, 4);
    for (std::uint32_t index = 0; index < 4; ++index) {
        bytes_[address + index] = static_cast<std::uint8_t>((value >> (8 * index)) & 0xFFU);
    }
}

void ScriptMemory::set_f32(ScriptAddress address, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    set_u32(address, bits);
}

void ScriptMemory::copy(ScriptAddress to, ScriptAddress from, std::uint32_t size) {
    check(from, size);
    check(to, size);
    if (size != 0) {
        std::memmove(bytes_.data() + to, bytes_.data() + from, size);
    }
}

std::uint32_t ScriptMemory::block_size(ScriptAddress block) const {
    const auto it = blocks_.find(block);
    if (it == blocks_.end()) {
        throw ScriptError("script memory block is not live");
    }
    return it->second;
}

}  // namespace bo2::engine
