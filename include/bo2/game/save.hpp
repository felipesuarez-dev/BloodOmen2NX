#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "bo2/game/player.hpp"

namespace bo2::game {

struct SaveGame {
    std::uint32_t version = 1;
    std::string level;
    PlayerState player;
    std::uint64_t tick = 0;
};

std::vector<std::uint8_t> serialize_save(const SaveGame& save);
SaveGame deserialize_save(const std::vector<std::uint8_t>& data);
std::uint64_t save_hash(const std::vector<std::uint8_t>& data);

class CheckpointSlots {
public:
    static constexpr std::uint32_t kSlotCount = 8;

    bool store(std::uint32_t slot, const SaveGame& save);
    std::optional<SaveGame> load(std::uint32_t slot) const;
    bool has(std::uint32_t slot) const;
    std::size_t count() const;

private:
    std::vector<std::optional<SaveGame>> slots_ = std::vector<std::optional<SaveGame>>(kSlotCount);
};

}  // namespace bo2::game
