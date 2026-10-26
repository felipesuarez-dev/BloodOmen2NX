#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "bo2/game/player.hpp"

namespace bo2::game {

struct HudState {
    float health = 100.0f;
    float mana = 100.0f;
    std::uint32_t weapon = 0;
};

struct SaveGame {
    std::uint32_t version = 1;
    std::string level;
    PlayerState player;
    HudState hud;
    std::uint64_t tick = 0;
};

std::vector<std::uint8_t> serialize_save(const SaveGame& save);
SaveGame deserialize_save(const std::vector<std::uint8_t>& data);
std::uint64_t save_hash(const std::vector<std::uint8_t>& data);

class Checkpoint {
public:
    Checkpoint(std::string level, const bo2::engine::Vec3& position, float radius);

    std::optional<SaveGame> update(const PlayerState& player, std::uint64_t tick,
                                   const HudState& hud);
    bool inside() const;
    bool triggered() const;
    void reset();

private:
    std::string level_;
    bo2::engine::Vec3 position_;
    float radius_;
    bool inside_ = false;
    bool triggered_ = false;
};

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
