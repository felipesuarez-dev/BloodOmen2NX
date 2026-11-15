#include "bo2/engine/pixmap_font.hpp"

#include <cstring>

#include "bo2/common/reader.hpp"

namespace bo2::engine {

namespace {

float read_float(const common::Reader& reader, std::size_t offset) {
    const std::uint32_t bits = reader.u32(offset);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

}  // namespace

const FontGlyph* PixmapFont::find(std::uint8_t code) const {
    for (const FontGlyph& glyph : glyphs) {
        if (glyph.code == code) {
            return &glyph;
        }
    }
    return nullptr;
}

// FUN_005df900: the loader reads the texture symbol, the four floats, the
// upper-case-only flag and the glyphs. With the flag set it adds the lower-case
// code of every capital that does not have one already.
PixmapFont parse_pixmap_font(const std::vector<std::uint8_t>& data,
                             const std::vector<std::uint16_t>& symbols,
                             const std::vector<std::string>& names) {
    constexpr std::size_t kHeaderSize = 40;
    constexpr std::size_t kGlyphSize = 20;
    if (data.size() < kHeaderSize) {
        throw common::ParseError("texfont entry is shorter than its header");
    }
    const common::Reader reader(data);
    const std::string tag(data.begin(), data.begin() + 8);
    if (tag != std::string("texfont\0", 8)) {
        throw common::ParseError("not a texfont entry");
    }
    if (reader.u32(8) != data.size()) {
        throw common::ParseError("texfont size does not match the payload");
    }
    const std::uint16_t texture_symbol = reader.u16(16);
    if (texture_symbol >= symbols.size() || symbols[texture_symbol] >= names.size()) {
        throw common::ParseError("texfont texture is outside the name table");
    }
    PixmapFont font;
    font.texture = names[symbols[texture_symbol]];
    // FUN_005ded00 writes them in this order: base height, kerning, leading
    // and space width.
    font.base_height = read_float(reader, 20);
    font.kerning = read_float(reader, 24);
    font.leading = read_float(reader, 28);
    font.space_width = read_float(reader, 32);
    font.upper_case_only = reader.u8(36) != 0;
    const std::uint16_t count = reader.u16(38);
    if (kHeaderSize + static_cast<std::size_t>(count) * kGlyphSize > data.size()) {
        throw common::ParseError("texfont glyphs are truncated");
    }
    font.glyphs.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        const std::size_t offset = kHeaderSize + static_cast<std::size_t>(index) * kGlyphSize;
        FontGlyph glyph;
        glyph.code = reader.u8(offset);
        glyph.x = read_float(reader, offset + 4);
        glyph.y = read_float(reader, offset + 8);
        glyph.width = read_float(reader, offset + 12);
        glyph.height = read_float(reader, offset + 16);
        font.glyphs.push_back(glyph);
    }
    if (font.upper_case_only) {
        for (std::uint8_t code = 'A'; code <= 'Z'; ++code) {
            const FontGlyph* upper = font.find(code);
            if (upper == nullptr ||
                font.find(static_cast<std::uint8_t>(code + 0x20)) != nullptr) {
                continue;
            }
            FontGlyph lower = *upper;
            lower.code = static_cast<std::uint8_t>(code + 0x20);
            font.glyphs.push_back(lower);
        }
    }
    return font;
}

}  // namespace bo2::engine
