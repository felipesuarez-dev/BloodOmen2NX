#include "bo2/engine/dds.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "bo2/common/reader.hpp"

namespace bo2::engine {

namespace {

constexpr std::size_t kHeaderBytes = 128;
constexpr std::uint32_t kPfFourCc = 0x4;
constexpr std::uint32_t kPfRgb = 0x40;
constexpr std::uint32_t kPfAlphaPixels = 0x1;
constexpr std::uint32_t kDxt1 = 0x31545844;
constexpr std::uint32_t kDxt3 = 0x33545844;
constexpr std::uint32_t kDxt5 = 0x35545844;
constexpr std::uint32_t kMaxDimension = 16384;
constexpr std::size_t kMaxPixels = 64U * 1024U * 1024U;

struct Color565 {
    std::uint16_t r = 0;
    std::uint16_t g = 0;
    std::uint16_t b = 0;

    static Color565 from(std::uint16_t value) {
        return {
            static_cast<std::uint16_t>((value >> 11) & 0x1F),
            static_cast<std::uint16_t>((value >> 5) & 0x3F),
            static_cast<std::uint16_t>(value & 0x1F),
        };
    }
};

struct Rgba {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

std::uint8_t expand5(std::uint16_t value) {
    return static_cast<std::uint8_t>((value << 3) | (value >> 2));
}

std::uint8_t expand6(std::uint16_t value) {
    return static_cast<std::uint8_t>((value << 2) | (value >> 4));
}

Rgba to_rgba(const Color565& color) {
    return {expand5(color.r), expand6(color.g), expand5(color.b), 255};
}

void decode_color_block(const std::uint8_t* block, bool dxt1, Rgba palette[4]) {
    const common::Reader reader(block, 8);
    const Color565 c0 = Color565::from(reader.u16(0));
    const Color565 c1 = Color565::from(reader.u16(2));
    palette[0] = to_rgba(c0);
    palette[1] = to_rgba(c1);

    if (dxt1 && reader.u16(0) <= reader.u16(2)) {
        palette[2] = to_rgba({
            static_cast<std::uint16_t>((c0.r + c1.r) / 2),
            static_cast<std::uint16_t>((c0.g + c1.g) / 2),
            static_cast<std::uint16_t>((c0.b + c1.b) / 2),
        });
        palette[3] = {0, 0, 0, 0};
    } else {
        palette[2] = to_rgba({
            static_cast<std::uint16_t>((2 * c0.r + c1.r) / 3),
            static_cast<std::uint16_t>((2 * c0.g + c1.g) / 3),
            static_cast<std::uint16_t>((2 * c0.b + c1.b) / 3),
        });
        palette[3] = to_rgba({
            static_cast<std::uint16_t>((c0.r + 2 * c1.r) / 3),
            static_cast<std::uint16_t>((c0.g + 2 * c1.g) / 3),
            static_cast<std::uint16_t>((c0.b + 2 * c1.b) / 3),
        });
    }
}

void decode_alpha_dxt3(const std::uint8_t* block, std::uint8_t alpha[16]) {
    for (int pixel = 0; pixel < 16; ++pixel) {
        const std::uint8_t packed = block[pixel / 2];
        const std::uint8_t nibble = static_cast<std::uint8_t>(
            (pixel % 2 == 0) ? (packed & 0x0F) : ((packed >> 4) & 0x0F));
        alpha[pixel] = static_cast<std::uint8_t>(nibble * 17);
    }
}

void decode_alpha_dxt5(const std::uint8_t* block, std::uint8_t alpha[16]) {
    const std::uint8_t a0 = block[0];
    const std::uint8_t a1 = block[1];
    std::array<std::uint8_t, 8> palette{};
    palette[0] = a0;
    palette[1] = a1;
    if (a0 > a1) {
        for (int index = 0; index < 6; ++index) {
            const int weight = 6 - index;
            palette[static_cast<std::size_t>(2 + index)] = static_cast<std::uint8_t>(
                (weight * a0 + (index + 1) * a1) / 7);
        }
    } else {
        for (int index = 0; index < 4; ++index) {
            const int weight = 4 - index;
            palette[static_cast<std::size_t>(2 + index)] = static_cast<std::uint8_t>(
                (weight * a0 + (index + 1) * a1) / 5);
        }
        palette[6] = 0;
        palette[7] = 255;
    }

    std::uint64_t bits = 0;
    for (int index = 0; index < 6; ++index) {
        bits |= static_cast<std::uint64_t>(block[2 + index]) << (8 * index);
    }
    for (int pixel = 0; pixel < 16; ++pixel) {
        const std::uint64_t code = (bits >> (3 * pixel)) & 0x7U;
        alpha[pixel] = palette[static_cast<std::size_t>(code)];
    }
}

std::uint8_t expand_channel(std::uint32_t value, std::uint32_t mask) {
    if (mask == 0) {
        return 0;
    }
    unsigned shift = 0;
    std::uint32_t shifted = mask;
    while ((shifted & 1U) == 0U && shift < 32U) {
        shifted >>= 1U;
        ++shift;
    }
    const std::uint32_t maximum = shifted;
    if (maximum == 0) {
        return 0;
    }
    const std::uint32_t channel = (value & mask) >> shift;
    return static_cast<std::uint8_t>((channel * 255U) / maximum);
}

}  // namespace

Image decode_dds(const std::vector<std::uint8_t>& data) {
    if (data.size() < kHeaderBytes) {
        throw common::ParseError("DDS file is shorter than its header");
    }
    const common::Reader reader(data);
    if (reader.u32(0) != 0x20534444U) {
        throw common::ParseError("not a DDS file");
    }
    if (reader.u32(4) != 124) {
        throw common::ParseError("bad DDS header size");
    }

    Image image;
    image.height = reader.u32(12);
    image.width = reader.u32(16);
    const std::uint32_t pitch = reader.u32(20);
    if (image.width == 0 || image.height == 0 || image.width > kMaxDimension ||
        image.height > kMaxDimension) {
        throw common::ParseError("unsupported DDS dimensions");
    }
    const std::size_t pixel_count = static_cast<std::size_t>(image.width) * image.height;
    if (pixel_count > kMaxPixels) {
        throw common::ParseError("DDS image is too large");
    }
    image.rgba.assign(pixel_count * 4, 0);

    if (reader.u32(76) != 32) {
        throw common::ParseError("unsupported DDS pixel format size");
    }
    const std::uint32_t pixel_flags = reader.u32(80);
    const std::uint32_t fourcc = reader.u32(84);
    const std::uint32_t rgb_bits = reader.u32(88);

    if ((pixel_flags & kPfFourCc) != 0) {
        const bool dxt1 = fourcc == kDxt1;
        const bool dxt3 = fourcc == kDxt3;
        const bool dxt5 = fourcc == kDxt5;
        if (!dxt1 && !dxt3 && !dxt5) {
            throw common::ParseError("unsupported DDS fourcc");
        }
        const std::size_t block_size = dxt1 ? 8 : 16;
        const std::size_t blocks_x = (static_cast<std::size_t>(image.width) + 3) / 4;
        const std::size_t blocks_y = (static_cast<std::size_t>(image.height) + 3) / 4;
        const std::size_t needed = blocks_x * blocks_y * block_size;
        if (data.size() < kHeaderBytes + needed) {
            throw common::ParseError("truncated DDS block data");
        }
        for (std::size_t block_y = 0; block_y < blocks_y; ++block_y) {
            for (std::size_t block_x = 0; block_x < blocks_x; ++block_x) {
                const std::uint8_t* block =
                    data.data() + kHeaderBytes + (block_y * blocks_x + block_x) * block_size;
                Rgba palette[4];
                decode_color_block(block + (dxt1 ? 0 : 8), dxt1, palette);
                std::uint8_t alpha[16];
                for (std::uint8_t& value : alpha) {
                    value = 255;
                }
                if (dxt3) {
                    decode_alpha_dxt3(block, alpha);
                } else if (dxt5) {
                    decode_alpha_dxt5(block, alpha);
                }
                const std::uint32_t indices = reader.u32(
                    static_cast<std::size_t>(block - data.data()) + (dxt1 ? 4 : 12));
                for (std::size_t pixel_y = 0; pixel_y < 4; ++pixel_y) {
                    for (std::size_t pixel_x = 0; pixel_x < 4; ++pixel_x) {
                        const std::size_t x = block_x * 4 + pixel_x;
                        const std::size_t y = block_y * 4 + pixel_y;
                        if (x >= image.width || y >= image.height) {
                            continue;
                        }
                        const std::size_t code = (indices >> (2 * (pixel_y * 4 + pixel_x))) & 0x3U;
                        Rgba color = palette[code];
                        if (!dxt1) {
                            color.a = alpha[pixel_y * 4 + pixel_x];
                        }
                        const std::size_t out = (y * image.width + x) * 4;
                        image.rgba[out + 0] = color.r;
                        image.rgba[out + 1] = color.g;
                        image.rgba[out + 2] = color.b;
                        image.rgba[out + 3] = color.a;
                    }
                }
            }
        }
        return image;
    }

    if ((pixel_flags & kPfRgb) == 0 || rgb_bits != 32) {
        throw common::ParseError("unsupported DDS pixel format");
    }
    const std::uint32_t red_mask = reader.u32(92);
    const std::uint32_t green_mask = reader.u32(96);
    const std::uint32_t blue_mask = reader.u32(100);
    const std::uint32_t alpha_mask = reader.u32(104);
    const std::size_t row_bytes = static_cast<std::size_t>(image.width) * 4;
    const std::size_t stride = pitch >= row_bytes ? pitch : row_bytes;
    const std::size_t needed = (static_cast<std::size_t>(image.height) - 1) * stride + row_bytes;
    if (data.size() < kHeaderBytes + needed) {
        throw common::ParseError("truncated DDS pixel data");
    }
    for (std::size_t y = 0; y < image.height; ++y) {
        for (std::size_t x = 0; x < image.width; ++x) {
            const std::uint32_t value = reader.u32(kHeaderBytes + y * stride + x * 4);
            const std::size_t out = (y * image.width + x) * 4;
            image.rgba[out + 0] = expand_channel(value, red_mask);
            image.rgba[out + 1] = expand_channel(value, green_mask);
            image.rgba[out + 2] = expand_channel(value, blue_mask);
            image.rgba[out + 3] = (alpha_mask != 0) ? expand_channel(value, alpha_mask) : 255;
        }
    }
    return image;
}

}  // namespace bo2::engine
