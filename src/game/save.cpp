#include "bo2/game/save.hpp"

#include <cstring>
#include <stdexcept>

namespace bo2::game {

namespace {

constexpr char kMagic[8] = {'B', 'O', '2', 'S', 'A', 'V', 'E', '\0'};

void put_u32(std::vector<std::uint8_t>& data, std::uint32_t value) {
    for (int index = 0; index < 4; ++index) {
        data.push_back(static_cast<std::uint8_t>((value >> (8 * index)) & 0xFF));
    }
}

void put_u64(std::vector<std::uint8_t>& data, std::uint64_t value) {
    for (int index = 0; index < 8; ++index) {
        data.push_back(static_cast<std::uint8_t>((value >> (8 * index)) & 0xFF));
    }
}

void put_float(std::vector<std::uint8_t>& data, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    put_u32(data, bits);
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& data, std::size_t& offset) {
    if (offset + 4 > data.size()) {
        throw std::runtime_error("truncated save");
    }
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(data[offset + static_cast<std::size_t>(index)])
                 << (8 * index);
    }
    offset += 4;
    return value;
}

std::uint64_t read_u64(const std::vector<std::uint8_t>& data, std::size_t& offset) {
    if (offset + 8 > data.size()) {
        throw std::runtime_error("truncated save");
    }
    std::uint64_t value = 0;
    for (int index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(data[offset + static_cast<std::size_t>(index)])
                 << (8 * index);
    }
    offset += 8;
    return value;
}

float read_float(const std::vector<std::uint8_t>& data, std::size_t& offset) {
    const std::uint32_t bits = read_u32(data, offset);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

}  // namespace

std::vector<std::uint8_t> serialize_save(const SaveGame& save) {
    std::vector<std::uint8_t> data(kMagic, kMagic + 8);
    put_u32(data, save.version);
    put_u32(data, static_cast<std::uint32_t>(save.level.size()));
    data.insert(data.end(), save.level.begin(), save.level.end());
    put_float(data, save.player.position.x);
    put_float(data, save.player.position.y);
    put_float(data, save.player.position.z);
    put_float(data, save.player.yaw);
    put_float(data, save.player.pitch);
    put_float(data, save.player.vertical_velocity);
    data.push_back(save.player.grounded ? 1 : 0);
    put_u64(data, save.tick);
    return data;
}

SaveGame deserialize_save(const std::vector<std::uint8_t>& data) {
    if (data.size() < 16 || std::memcmp(data.data(), kMagic, 8) != 0) {
        throw std::runtime_error("bad save magic");
    }
    std::size_t offset = 8;
    SaveGame save;
    save.version = read_u32(data, offset);
    if (save.version != 1) {
        throw std::runtime_error("unsupported save version");
    }
    const std::uint32_t length = read_u32(data, offset);
    if (length > 1024 || offset + length > data.size()) {
        throw std::runtime_error("bad save level name");
    }
    save.level.assign(data.begin() + static_cast<std::ptrdiff_t>(offset),
                      data.begin() + static_cast<std::ptrdiff_t>(offset + length));
    offset += length;
    save.player.position.x = read_float(data, offset);
    save.player.position.y = read_float(data, offset);
    save.player.position.z = read_float(data, offset);
    save.player.yaw = read_float(data, offset);
    save.player.pitch = read_float(data, offset);
    save.player.vertical_velocity = read_float(data, offset);
    if (offset >= data.size()) {
        throw std::runtime_error("truncated save flags");
    }
    save.player.grounded = data[offset++] != 0;
    save.tick = read_u64(data, offset);
    if (offset != data.size()) {
        throw std::runtime_error("save has trailing bytes");
    }
    return save;
}

std::uint64_t save_hash(const std::vector<std::uint8_t>& data) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::uint8_t value : data) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool CheckpointSlots::store(std::uint32_t slot, const SaveGame& save) {
    if (slot >= slots_.size()) {
        return false;
    }
    slots_[slot] = save;
    return true;
}

std::optional<SaveGame> CheckpointSlots::load(std::uint32_t slot) const {
    if (slot >= slots_.size()) {
        return std::nullopt;
    }
    return slots_[slot];
}

bool CheckpointSlots::has(std::uint32_t slot) const {
    return slot < slots_.size() && slots_[slot].has_value();
}

std::size_t CheckpointSlots::count() const {
    std::size_t total = 0;
    for (const auto& slot : slots_) {
        if (slot.has_value()) {
            ++total;
        }
    }
    return total;
}

}  // namespace bo2::game
