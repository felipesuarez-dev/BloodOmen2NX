#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "bo2/common/reader.hpp"
#include "bo2/engine/anim_reel.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

using Bytes = std::vector<std::uint8_t>;

void put_u16(Bytes& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void put_u32(Bytes& out, std::uint32_t value) {
    put_u16(out, static_cast<std::uint16_t>(value & 0xFFFF));
    put_u16(out, static_cast<std::uint16_t>(value >> 16));
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

void put_f32(Bytes& out, float value) {
    put_u32(out, float_bits(value));
}

void pad4(Bytes& out) {
    while (out.size() % 4 != 0) {
        out.push_back(0);
    }
}

struct Channel {
    std::uint16_t node;
    std::uint8_t type;
    std::uint8_t component;
    std::uint32_t value;
    Bytes blob;
};

Bytes lsc(float offset, float scale, float start, const std::vector<std::uint16_t>& segments) {
    Bytes out;
    put_f32(out, offset);
    put_f32(out, scale);
    put_f32(out, start);
    put_u16(out, static_cast<std::uint16_t>(segments.size()));
    for (const std::uint16_t segment : segments) {
        put_u16(out, segment);
    }
    pad4(out);
    return out;
}

Bytes lc(float offset, float scale, const std::vector<std::uint8_t>& samples) {
    Bytes out;
    put_f32(out, offset);
    put_f32(out, scale);
    out.insert(out.end(), samples.begin(), samples.end());
    pad4(out);
    return out;
}

Bytes uc(const std::vector<float>& samples) {
    Bytes out;
    for (const float sample : samples) {
        put_f32(out, sample);
    }
    return out;
}

Bytes make_reel(std::uint32_t frames, const std::vector<Channel>& channels,
                std::uint32_t inner = 6, std::uint32_t chunk_version = 4) {
    Bytes body;
    put_u32(body, inner);
    put_u32(body, 0);  // reel+8
    put_u32(body, 1);  // one event
    put_u32(body, 7);
    put_f32(body, 5.0f);
    Bytes blob;
    for (const Channel& channel : channels) {
        blob.insert(blob.end(), channel.blob.begin(), channel.blob.end());
    }
    put_f32(body, 30.0f);
    put_u32(body, frames);
    put_u32(body, static_cast<std::uint32_t>(channels.size()));
    put_u32(body, static_cast<std::uint32_t>(blob.size()));
    body.insert(body.end(), blob.begin(), blob.end());
    for (const Channel& channel : channels) {
        put_u16(body, channel.node);
        body.push_back(channel.type);
        body.push_back(channel.component);
        put_u32(body, channel.value);
    }
    Bytes out = {'a', 'n', 'i', 'm', 'r', 'e', 'e', 'l'};
    put_u32(out, static_cast<std::uint32_t>(16 + body.size()));
    put_u32(out, chunk_version);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

std::uint32_t constant_value(float value) {
    return float_bits(value) | 1U;
}

bool near(float left, float right) {
    return std::fabs(left - right) < 1.0e-4f;
}

bool throws_parse_error(const Bytes& bytes) {
    try {
        bo2::engine::parse_anim_reel(bytes);
    } catch (const bo2::common::ParseError&) {
        return true;
    }
    return false;
}

void test_header_and_duration() {
    const auto reel = bo2::engine::parse_anim_reel(make_reel(31, {{0, 1, 0, constant_value(2.0f), {}}}));
    CHECK(reel.fps == 30.0f);
    CHECK(reel.frames == 31);
    CHECK(near(reel.duration, 1.0f));
    CHECK(reel.channels.size() == 1);
}

void test_constant_keeps_the_tag_bit() {
    const std::uint32_t bits = 0x3F800001U;  // 1.0 plus one ulp
    const auto reel = bo2::engine::parse_anim_reel(make_reel(3, {{2, 5, 1, bits, {}}}));
    CHECK(reel.channels[0].constant);
    CHECK(float_bits(reel.channels[0].value) == bits);
    CHECK(reel.channels[0].at(2) == reel.channels[0].value);
}

void test_uc_lc_lsc_decode() {
    const std::vector<Channel> channels = {
        {1, 1, 0, 0, uc({1.0f, 2.5f, -4.0f})},
        {1, 1, 1, 12, lc(10.0f, 0.5f, {0, 2, 4})},
        // Segments of 1 and 2 frames; deltas are raw * scale + offset.
        {1, 1, 2, 8, lsc(1.0f, 2.0f, 5.0f, {static_cast<std::uint16_t>((1 << 11) | 3),
                                            static_cast<std::uint16_t>((2 << 11) | 5)})},
    };
    const auto reel = bo2::engine::parse_anim_reel(make_reel(3, channels));
    CHECK(reel.channels[0].samples == (std::vector<float>{1.0f, 2.5f, -4.0f}));
    CHECK(reel.channels[1].samples == (std::vector<float>{10.0f, 11.0f, 12.0f}));
    // delta0 = 3 * 2 + 1 = 7, delta1 = 5 * 2 + 1 = 11 over 2 frames.
    CHECK(near(reel.channels[2].samples[0], 5.0f));
    CHECK(near(reel.channels[2].samples[1], 12.0f));
    CHECK(near(reel.channels[2].samples[2], 12.0f + 5.5f));
}

void test_lsc_clamps_to_the_last_segment() {
    const std::uint16_t segment = static_cast<std::uint16_t>((2 << 11) | 4);
    const auto reel =
        bo2::engine::parse_anim_reel(make_reel(5, {{1, 1, 0, 8, lsc(1.0f, 0.5f, 0.0f, {segment})}}));
    // delta = 3 over 2 frames: 1.5 per frame, past the end too.
    for (std::size_t frame = 0; frame < 5; ++frame) {
        CHECK(near(reel.channels[0].samples[frame], 1.5f * static_cast<float>(frame)));
    }
}

void test_blob_is_consumed_exactly() {
    const std::vector<Channel> channels = {
        {1, 1, 0, 0, uc({1.0f, 2.0f})},
        {1, 1, 1, constant_value(7.0f), {}},
        {1, 1, 2, 12, lc(0.0f, 1.0f, {3, 4})},
        {1, 1, 3, 16, {0, 0, 0, 0}},
    };
    const auto reel = bo2::engine::parse_anim_reel(make_reel(2, channels));
    CHECK(reel.channels[2].samples == (std::vector<float>{3.0f, 4.0f}));
    CHECK(reel.channels[3].opaque && reel.channels[3].samples.empty());

    std::vector<Channel> unread = {{1, 1, 0, 0, uc({1.0f, 2.0f})}};
    unread[0].blob.insert(unread[0].blob.end(), 4, 0);
    CHECK(throws_parse_error(make_reel(2, unread)));
    CHECK(throws_parse_error(make_reel(3, {{1, 1, 0, 0, uc({1.0f, 2.0f})}})));
}

void test_errors() {
    CHECK(throws_parse_error(make_reel(1, {}, 5)));
    CHECK(throws_parse_error(make_reel(1, {}, 6, 3)));
    CHECK(throws_parse_error(make_reel(1, {{1, 1, 0, 4, {}}})));
    Bytes trailing = make_reel(1, {});
    trailing.insert(trailing.end(), 4, 0);
    CHECK(throws_parse_error(trailing));  // size field no longer matches
    Bytes truncated = make_reel(1, {});
    truncated.pop_back();
    CHECK(throws_parse_error(truncated));
    Bytes tag = make_reel(1, {});
    tag[0] = 'x';
    CHECK(throws_parse_error(tag));
    CHECK(throws_parse_error(Bytes{}));
}

void test_limits_and_truncation() {
    // Frame count outside 1..65535.
    CHECK(throws_parse_error(make_reel(0, {})));
    CHECK(throws_parse_error(make_reel(65536, {})));
    CHECK(!throws_parse_error(make_reel(65535, {})));
    // Truncated LSC (cut inside the segments) and LC (fewer samples than frames).
    Bytes cut = lsc(0.0f, 1.0f, 0.0f, {1 << 11, 1 << 11, 1 << 11});
    cut.resize(cut.size() - 4);
    CHECK(throws_parse_error(make_reel(3, {{1, 1, 0, 8, cut}})));
    CHECK(throws_parse_error(make_reel(3, {{1, 1, 0, 8, Bytes(8, 0)}})));
    CHECK(throws_parse_error(make_reel(16, {{1, 1, 0, 12, lc(0.0f, 1.0f, {1, 2})}})));
    // An event count that overruns the buffer.
    Bytes events = make_reel(2, {});
    events[24] = 0xFF;
    events[25] = 0xFF;
    events[26] = 0xFF;
    events[27] = 0x7F;
    CHECK(throws_parse_error(events));
    // 257 channels of 65535 samples exceed the 16M decoded-sample limit; 256 do not.
    const Bytes tiny = lsc(0.0f, 1.0f, 1.0f, {});
    std::vector<Channel> many(256, Channel{1, 1, 0, 8, tiny});
    CHECK(!throws_parse_error(make_reel(65535, many)));
    many.push_back(Channel{1, 1, 0, 8, tiny});
    CHECK(throws_parse_error(make_reel(65535, many)));
    // The largest K must expand in one pass (a per-frame scan would not finish).
    const std::vector<std::uint16_t> unit_segments(65535, static_cast<std::uint16_t>((1 << 11) | 1));
    const auto big =
        bo2::engine::parse_anim_reel(make_reel(65535, {{1, 1, 0, 8, lsc(0.0f, 1.0f, 0.0f, unit_segments)}}));
    CHECK(near(big.channels[0].samples[65534], 65534.0f));
}

void test_root_displacement() {
    const std::vector<Channel> channels = {
        {0, 1, 0, 8, lsc(0.0f, 1.0f, 0.0f, {static_cast<std::uint16_t>((30 << 11) | 30)})},
        {0, 1, 2, constant_value(9.0f), {}},   // constant z: no displacement
        {0, 19, 0, constant_value(100.0f), {}},  // other type: ignored
        {3, 1, 1, 0, uc(std::vector<float>(31, 0.0f))},  // other node: ignored
    };
    const auto reel = bo2::engine::parse_anim_reel(make_reel(31, channels));
    const bo2::engine::Vec3 displacement = bo2::engine::root_displacement(reel);
    CHECK(near(displacement.x, 30.0f));
    CHECK(displacement.y == 0.0f);
    CHECK(displacement.z == 0.0f);
}

}  // namespace

int main() {
    test_header_and_duration();
    test_constant_keeps_the_tag_bit();
    test_uc_lc_lsc_decode();
    test_lsc_clamps_to_the_last_segment();
    test_limits_and_truncation();
    test_blob_is_consumed_exactly();
    test_errors();
    test_root_displacement();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "anim_reel tests: ok\n";
    return 0;
}
