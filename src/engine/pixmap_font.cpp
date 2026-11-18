#include "bo2/engine/pixmap_font.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

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

// FUN_005df900: the rectangle of a glyph is measured from the bottom of the
// texture, so its first row is height - y - glyph height. The alpha channel
// is copied with the rows from top to bottom.
GlyphBitmap glyph_bitmap(const PixmapFont& font, const Image& texture, std::uint8_t code) {
    const FontGlyph* glyph = font.find(code);
    if (glyph == nullptr) {
        throw std::out_of_range("font has no glyph for the code");
    }
    if (texture.width == 0 || texture.height == 0 ||
        texture.rgba.size() <
            static_cast<std::size_t>(texture.width) * texture.height * 4) {
        throw common::ParseError("font texture is invalid");
    }
    if (glyph->x < 0.0f || glyph->y < 0.0f || glyph->width <= 0.0f ||
        glyph->height <= 0.0f ||
        glyph->x + glyph->width > static_cast<float>(texture.width) ||
        glyph->y + glyph->height > static_cast<float>(texture.height)) {
        throw common::ParseError("glyph rectangle is outside the texture");
    }
    const std::uint32_t width = static_cast<std::uint32_t>(glyph->width);
    const std::uint32_t height = static_cast<std::uint32_t>(glyph->height);
    const std::uint32_t left = static_cast<std::uint32_t>(glyph->x);
    const std::uint32_t first_row =
        texture.height - static_cast<std::uint32_t>(glyph->y + glyph->height);
    GlyphBitmap bitmap;
    bitmap.width = width;
    bitmap.height = height;
    bitmap.alpha.resize(static_cast<std::size_t>(width) * height);
    for (std::uint32_t row = 0; row < height; ++row) {
        for (std::uint32_t column = 0; column < width; ++column) {
            const std::size_t source =
                (static_cast<std::size_t>(first_row + row) * texture.width + left + column) *
                4;
            bitmap.alpha[static_cast<std::size_t>(row) * width + column] =
                texture.rgba[source + 3];
        }
    }
    return bitmap;
}

// FUN_00575980: every byte advances the pen or places a glyph. A newline
// returns the pen to the starting x and lowers it one row; a space advances
// space_width; a glyph takes the rectangle x..x + width * scale and
// y - row_height..y and advances (width + kerning) * scale. A code the font
// does not have falls back to ^, and to a space when ^ is missing too. The
// texture is stored upside down, so the top of the rectangle takes the
// bottom row of the glyph.
StringLayout layout_string(const PixmapFont& font, std::uint32_t texture_width,
                           std::uint32_t texture_height, std::string_view text, float x,
                           float y, float row_height, float scale) {
    StringLayout layout;
    layout.pen_x = x;
    layout.pen_y = y;
    const float start_x = x;
    const float texture_width_value = static_cast<float>(texture_width);
    const float texture_height_value = static_cast<float>(texture_height);
    for (const char raw : text) {
        const std::uint8_t code = static_cast<std::uint8_t>(raw);
        if (code == 0x0a) {
            layout.pen_y += row_height;
            layout.pen_x = start_x;
            continue;
        }
        if (code == 0x20) {
            layout.pen_x += font.space_width * scale;
            continue;
        }
        const FontGlyph* glyph = font.find(code);
        std::uint8_t used = code;
        if (glyph == nullptr) {
            glyph = font.find('^');
            used = '^';
        }
        if (glyph == nullptr) {
            layout.pen_x += font.space_width * scale;
            continue;
        }
        GlyphQuad quad;
        quad.code = used;
        quad.x0 = layout.pen_x;
        quad.x1 = layout.pen_x + glyph->width * scale;
        quad.y0 = layout.pen_y - row_height;
        quad.y1 = layout.pen_y;
        quad.u0 = glyph->x / texture_width_value;
        quad.u1 = (glyph->x + glyph->width) / texture_width_value;
        quad.v0 = (texture_height_value - glyph->y) / texture_height_value;
        quad.v1 =
            (texture_height_value - glyph->y - glyph->height) / texture_height_value;
        layout.quads.push_back(quad);
        layout.pen_x += (glyph->width + font.kerning) * scale;
    }
    return layout;
}

// FUN_0056e2b0: the y scale of a row of `size` screen units, over the
// leading and the base height of the font, and the x scale from the aspect.
TextStyle make_text_style(const PixmapFont& font, float size, float aspect, float half_height) {
    TextStyle style;
    style.y_scale = size * half_height / (font.leading + font.base_height);
    style.x_scale = style.y_scale * aspect;
    return style;
}

// FUN_0056df40: the widest row and the sum of the row heights of a string.
// The glyph of ^ is not a replacement here; that is only done when the glyphs
// are placed.
TextSize measure_text(const PixmapFont& font, const TextStyle& style, std::string_view text,
                      float half_width, float half_height) {
    float widest = 0.0f;
    float row_width = 0.0f;
    float row_height = 0.0f;
    float total_height = 0.0f;
    for (const char raw : text) {
        const std::uint8_t code = static_cast<std::uint8_t>(raw);
        if (code == 0x0a) {
            widest = std::max(widest, row_width);
            total_height += row_height;
            row_width = 0.0f;
            row_height = 0.0f;
            continue;
        }
        const FontGlyph* glyph = font.find(code);
        if (code == 0x20 || glyph == nullptr) {
            row_width += font.space_width * style.x_scale;
            if (row_height == 0.0f) {
                row_height = (font.leading + font.base_height) * style.y_scale;
            }
            continue;
        }
        row_width += (font.kerning + glyph->width) * style.x_scale;
        row_height =
            std::max(row_height, (font.leading + glyph->height) * style.y_scale);
    }
    widest = std::max(widest, row_width);
    TextSize size;
    size.width = widest / half_width;
    size.height = (total_height + row_height) / half_height;
    return size;
}

// FUN_0056e300: the pen in the 640 x 480 text space, the height of a row and
// the scale layout_string takes.
TextPen text_pen(const PixmapFont& font, const TextStyle& style, float x, float y,
                 float half_width, float half_height) {
    TextPen pen;
    pen.x = x * half_width + half_width;
    pen.y = half_height - y * half_height;
    pen.row_height = (font.leading + font.base_height) * style.y_scale;
    pen.scale = style.x_scale;
    return pen;
}

}  // namespace bo2::engine
