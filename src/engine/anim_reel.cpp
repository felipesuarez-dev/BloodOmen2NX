#include "bo2/engine/anim_reel.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include "bo2/common/reader.hpp"

namespace bo2::engine {

namespace {

constexpr char kTag[8] = {'a', 'n', 'i', 'm', 'r', 'e', 'e', 'l'};
constexpr std::uint32_t kChunkVersion = 4;  // FUN_0066aa20 also accepts 1, 3, 0x10001, 0x20001
constexpr std::uint32_t kInnerVersion = 6;  // FUN_00668700
constexpr double kFrameTime = 1.0 / 30.0;   // double at 0x872498

constexpr std::uint32_t kCodecUC = 0;       // AnimChanUC, FUN_0067ad20
constexpr std::uint32_t kCodecLSC = 8;      // AnimChanLSC, FUN_0068b1f0
constexpr std::uint32_t kCodecLC = 12;      // AnimChanLC, FUN_0068aca0
constexpr std::uint32_t kCodecStream = 16;  // AnimChanStream, FUN_006712c0

constexpr std::uint8_t kRootNode = 0;
constexpr std::uint8_t kTranslation = 1;  // inferred, docs/FORMATS/animreel.md

// The codec constructors (FUN_0067ad20, FUN_0068aca0, FUN_0068b1f0) take the
// frame count as a ushort and the reel keeps it as a short (the reel+0x12 copy
// in FUN_0063c480).
constexpr std::uint32_t kMaxFrames = 65535;
// Port-only sanity limit on decoded samples per reel, so 16-byte LSC channels
// cannot amplify a small file into gigabytes. Not an original limit.
constexpr std::size_t kMaxSamples = 16U * 1024U * 1024U;

std::size_t align4(std::size_t value) {
    return (value + 3) & ~static_cast<std::size_t>(3);
}

float float_from_bits(std::uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

float read_float(const common::Reader& reader, std::size_t offset) {
    return float_from_bits(reader.u32(offset));
}

// FUN_0068aed0 at every integer frame (fraction 0), in one pass: O(frames + K).
// Past the last segment this port clamps to it and keeps extrapolating. In the
// original the loop advances to index K and reads the u16 one past the segment
// array (padding or the next channel's data) before its `K <= index` check. A
// zero-length segment divides by zero in the original; the port returns the
// running value. No Kain clip reaches either path.
void expand_lsc(const common::Reader& reader, std::size_t channel, std::size_t frames,
                std::vector<float>& out) {
    const double offset = read_float(reader, channel);
    const double scale = read_float(reader, channel + 4);
    double value = read_float(reader, channel + 8);
    const std::size_t count = reader.u16(channel + 12);
    out.resize(frames);
    if (count == 0) {
        std::fill(out.begin(), out.end(), static_cast<float>(value));
        return;
    }
    const std::size_t segments = channel + 14;
    std::size_t index = 0;
    std::size_t base = 0;  // frames covered by the segments before `index`
    std::uint16_t segment = reader.u16(segments);
    std::size_t length = static_cast<std::size_t>(segment >> 11);
    double delta = (segment & 0x7FF) * scale + offset;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        while (length < frame - base && index + 1 < count) {
            value += delta;
            base += length;
            ++index;
            segment = reader.u16(segments + index * 2);
            length = static_cast<std::size_t>(segment >> 11);
            delta = (segment & 0x7FF) * scale + offset;
        }
        out[frame] = static_cast<float>(
            length == 0 ? value : value + static_cast<double>(frame - base) * delta /
                                              static_cast<double>(length));
    }
}

}  // namespace

AnimReel parse_anim_reel(const std::vector<std::uint8_t>& bytes) {
    const common::Reader reader(bytes);
    reader.require(0, 28);
    if (std::memcmp(bytes.data(), kTag, sizeof kTag) != 0) {
        throw common::ParseError("not an animreel chunk");
    }
    if (reader.u32(8) != bytes.size()) {
        throw common::ParseError("animreel size field does not match the payload");
    }
    if (reader.u32(12) != kChunkVersion) {
        throw common::ParseError("unsupported animreel chunk version " +
                                 std::to_string(reader.u32(12)));
    }
    if (reader.u32(16) != kInnerVersion) {
        throw common::ParseError("unsupported animreel inner version " +
                                 std::to_string(reader.u32(16)));
    }
    // reader.u32(20) is reel+8, unknown.
    std::size_t cursor = 24;
    const std::size_t event_count = reader.u32(cursor);
    cursor += 4;
    reader.require(cursor, event_count * 8);
    cursor += event_count * 8;  // {symbol id, frame} pairs are not used yet

    AnimReel reel;
    reader.require(cursor, 16);
    reel.fps = read_float(reader, cursor);
    reel.frames = reader.u32(cursor + 4);
    const std::size_t descriptor_count = reader.u32(cursor + 8);
    const std::size_t blob_size = reader.u32(cursor + 12);
    cursor += 16;
    if (reel.frames == 0 || reel.frames > kMaxFrames) {
        throw common::ParseError("animreel frame count is outside 1..65535");
    }
    const std::size_t blob = cursor;
    reader.require(blob, blob_size);
    const std::size_t descriptors = blob + blob_size;
    reader.require(descriptors, descriptor_count * 8);
    if (descriptors + descriptor_count * 8 != bytes.size()) {
        throw common::ParseError("animreel has trailing bytes after the descriptors");
    }
    reel.duration = static_cast<float>((reel.frames - 1) * kFrameTime);

    const std::size_t frames = reel.frames;
    std::size_t used = 0;  // blob bytes consumed so far
    std::size_t samples_left = kMaxSamples;
    reel.channels.reserve(descriptor_count);
    for (std::size_t index = 0; index < descriptor_count; ++index) {
        const std::size_t at = descriptors + index * 8;
        AnimChannel channel;
        channel.node = reader.u16(at);
        channel.type = reader.u8(at + 2);
        channel.component = reader.u8(at + 3);
        const std::uint32_t value = reader.u32(at + 4);
        if ((value & 1U) != 0) {
            // Constant: the bit pattern is the float, tag bit included.
            channel.constant = true;
            channel.value = float_from_bits(value);
        } else if (value == kCodecUC || value == kCodecLSC || value == kCodecLC) {
            if (frames > samples_left) {
                throw common::ParseError("animreel decoded sample limit exceeded");
            }
            samples_left -= frames;
            if (value == kCodecUC) {
                if (frames * 4 > blob_size - used) {
                    throw common::ParseError("animreel UC channel is truncated");
                }
                channel.samples.resize(frames);
                for (std::size_t frame = 0; frame < frames; ++frame) {
                    channel.samples[frame] = read_float(reader, blob + used + frame * 4);
                }
                used += frames * 4;
            } else if (value == kCodecLSC) {
                if (blob_size - used < 14) {
                    throw common::ParseError("animreel LSC channel is truncated");
                }
                const std::size_t size =
                    align4(reader.u16(blob + used + 12) * std::size_t{2} + 2) + 12;
                if (size > blob_size - used) {
                    throw common::ParseError("animreel LSC channel is truncated");
                }
                expand_lsc(reader, blob + used, frames, channel.samples);
                used += size;
            } else {
                const std::size_t size = align4(frames + 8);
                if (size > blob_size - used) {
                    throw common::ParseError("animreel LC channel is truncated");
                }
                const double offset = read_float(reader, blob + used);
                const double scale = read_float(reader, blob + used + 4);
                channel.samples.resize(frames);
                for (std::size_t frame = 0; frame < frames; ++frame) {
                    channel.samples[frame] = static_cast<float>(
                        reader.u8(blob + used + 8 + frame) * scale + offset);
                }
                used += size;
            }
        } else if (value == kCodecStream) {
            if (blob_size - used < 4) {
                throw common::ParseError("animreel stream channel is truncated");
            }
            channel.opaque = true;
            used += 4;
        } else {
            throw common::ParseError("unknown animreel channel codec " + std::to_string(value));
        }
        reel.channels.push_back(std::move(channel));
    }
    if (used != blob_size) {
        throw common::ParseError("animreel blob has unread bytes");
    }
    return reel;
}

Vec3 root_displacement(const AnimReel& reel) {
    Vec3 out{0.0f, 0.0f, 0.0f};
    for (const AnimChannel& channel : reel.channels) {
        if (channel.node != kRootNode || channel.type != kTranslation || channel.opaque ||
            channel.component > 2) {
            continue;
        }
        const float delta = channel.at(reel.frames - 1) - channel.at(0);
        (channel.component == 0 ? out.x : channel.component == 1 ? out.y : out.z) = delta;
    }
    return out;
}

}  // namespace bo2::engine
