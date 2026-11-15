#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace bo2::engine {

// The memory of the script machine (docs/ARCHITECTURE.md, "Script machine"):
// one byte arena addressed by 32-bit offsets, because the compiled scripts
// use 4-byte cells and 4-byte pointers. Address 0 is null and no block starts
// below 8. Blocks are aligned to 4 and start zeroed. Every access is checked:
// a read, a write or a copy that does not fall inside one live block throws
// ScriptError.
using ScriptAddress = std::uint32_t;

class ScriptError : public std::runtime_error {
public:
    explicit ScriptError(const std::string& message) : std::runtime_error(message) {}
};

class ScriptMemory {
public:
    // Reserves a zeroed block of `size` bytes (4 when `size` is 0) at the
    // first gap, from address 8, that holds it. Throws when the block would
    // end past 0xfffffff0.
    ScriptAddress allocate(std::uint32_t size);
    // Frees the block that starts at `block`.
    void release(ScriptAddress block);

    std::uint8_t u8(ScriptAddress address) const;
    std::uint32_t u32(ScriptAddress address) const;
    float f32(ScriptAddress address) const;
    void set_u8(ScriptAddress address, std::uint8_t value);
    void set_u32(ScriptAddress address, std::uint32_t value);
    void set_f32(ScriptAddress address, float value);
    // Copies `size` bytes; each range must lie inside one live block. The
    // ranges may overlap.
    void copy(ScriptAddress to, ScriptAddress from, std::uint32_t size);

    // The size the block was reserved with (4 for a reservation of 0).
    std::uint32_t block_size(ScriptAddress block) const;
    std::size_t live_blocks() const { return blocks_.size(); }

private:
    // Throws unless [address, address + size) lies inside one live block.
    void check(ScriptAddress address, std::uint32_t size) const;

    std::vector<std::uint8_t> bytes_;
    std::map<ScriptAddress, std::uint32_t> blocks_;  // start, size
};

}  // namespace bo2::engine
