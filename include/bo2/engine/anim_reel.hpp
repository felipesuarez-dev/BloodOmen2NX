#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "bo2/engine/math.hpp"

namespace bo2::engine {

// One channel of an `animreel` clip (docs/FORMATS/animreel.md). A channel is
// either a constant or one float per frame. AnimChanStream channels carry no
// decoded data: `opaque` is set and `samples` is empty.
struct AnimChannel {
    std::uint16_t node = 0;      // the bone id (`boneIdx`), or a flagged auxiliary node
    std::uint8_t type = 0;       // 0 rotation, 1 translation, 2 scale (FUN_00638920)
    std::uint8_t component = 0;  // 0, 1, 2 = x, y, z
    bool constant = false;
    bool opaque = false;
    float value = 0.0f;          // the constant, when `constant` is set
    std::vector<float> samples;  // `frames` floats, empty for constants and streams

    float at(std::uint32_t frame) const { return constant ? value : samples.at(frame); }

    // The value at a fractional frame, clamped to the clip: linear between the
    // two neighbouring frames (FUN_0067aaf0 for UC, FUN_0068aa90 for LC; the
    // LSC segments are linear over whole frames, so the same rule is exact).
    // A stream channel has no decoded data and samples as 0.
    float sample(float frame) const {
        if (constant) {
            return value;
        }
        if (samples.empty()) {
            return 0.0f;
        }
        const float last = static_cast<float>(samples.size() - 1);
        const float clamped = frame < 0.0f ? 0.0f : (frame > last ? last : frame);
        const std::size_t index = static_cast<std::size_t>(clamped);
        const float fraction = clamped - static_cast<float>(index);
        if (fraction == 0.0f || index + 1 >= samples.size()) {
            return samples[index];
        }
        return samples[index] + (samples[index + 1] - samples[index]) * fraction;
    }
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

// The index of the channel with this descriptor key, or -1. The original
// bisects the descriptors by `node << 16 | type << 8 | component`
// (FUN_0063ce20); the port scans them and returns the first match, which is the
// same channel because the bone keys are sorted and unique. Only the flagged
// auxiliary nodes (0x1000 and up) repeat a key in some clips.
int find_channel(const AnimReel& reel, std::uint16_t node, std::uint8_t type,
                 std::uint8_t component);

}  // namespace bo2::engine
