#pragma once

#include <cstdint>
#include <vector>

#include "bo2/engine/math.hpp"

namespace bo2::engine {

// One channel of an `animreel` clip (docs/FORMATS/animreel.md). A channel is
// either a constant or one float per frame. AnimChanStream channels carry no
// decoded data: `opaque` is set and `samples` is empty.
struct AnimChannel {
    std::uint16_t node = 0;
    std::uint8_t type = 0;       // 1 = translation (inferred)
    std::uint8_t component = 0;  // 0, 1, 2 = x, y, z for translation
    bool constant = false;
    bool opaque = false;
    float value = 0.0f;          // the constant, when `constant` is set
    std::vector<float> samples;  // `frames` floats, empty for constants and streams

    float at(std::uint32_t frame) const { return constant ? value : samples.at(frame); }
};

struct AnimReel {
    float fps = 0.0f;
    std::uint32_t frames = 0;
    float duration = 0.0f;  // (frames - 1) / 30, FUN_0068ada0
    std::vector<AnimChannel> channels;
};

// `bytes` is the extracted `animreel` entry. Only inner version 6 is read
// (FUN_00668700). Throws common::ParseError on any other version, an unknown
// channel codec, a truncated payload, an unread blob tail or trailing bytes,
// a frame count outside 1..65535 or more than 16M decoded
// samples (a port-only limit).
AnimReel parse_anim_reel(const std::vector<std::uint8_t>& bytes);

// Root translation over one cycle: value(last frame) - value(first frame) of
// node 0, type 1, components 0..2 (local x is forward). Missing components
// are 0. The root is matched as node == 0 exactly; flagged nodes (>= 0x1000)
// are not treated as the root.
Vec3 root_displacement(const AnimReel& reel);

}  // namespace bo2::engine
