#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "bo2/engine/audio.hpp"

namespace bo2::engine {

std::shared_ptr<const PcmClip> decode_ogg(const std::uint8_t* data, std::size_t size);
std::shared_ptr<const PcmClip> decode_ogg(const std::vector<std::uint8_t>& data);

}  // namespace bo2::engine
