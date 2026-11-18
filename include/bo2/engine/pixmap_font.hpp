#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bo2/engine/image.hpp"

namespace bo2::engine {

// One glyph of the game font: its byte code and its rectangle in the font
// texture, in texels, with y measured from the bottom of the image
// (FUN_005df900).
struct FontGlyph {
    std::uint8_t code;
    float x;
    float y;
    float width;
    float height;
};

// The `texfont` entry of `code.big`: the name of the font texture, the four
// header values, the upper-case-only flag and the glyphs. Loader FUN_005df900;
// the header order is the one the converter FUN_005ded00 writes.
struct PixmapFont {
    std::string texture;
    float base_height = 0.0f;
    float kerning = 0.0f;
    float leading = 0.0f;
    float space_width = 0.0f;
    bool upper_case_only = false;
    std::vector<FontGlyph> glyphs;

    // The first glyph with `code`, or nullptr.
    const FontGlyph* find(std::uint8_t code) const;
};

// FUN_005df900: reads a `texfont` entry. `symbols` is the symbol list of the
// entry and `names` the name table of the container, as in parse_cell_group.
// Throws common::ParseError when the entry is not well formed.
PixmapFont parse_pixmap_font(const std::vector<std::uint8_t>& data,
                             const std::vector<std::uint16_t>& symbols,
                             const std::vector<std::string>& names);

// The alpha shape of one glyph, with its rows from top to bottom.
struct GlyphBitmap {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> alpha;
};

// FUN_005df900: the rectangle of a glyph is measured from the bottom of the
// texture; this returns its alpha channel. Throws std::out_of_range when the
// font has no such code and common::ParseError when the rectangle is outside
// the texture.
GlyphBitmap glyph_bitmap(const PixmapFont& font, const Image& texture, std::uint8_t code);

// One placed glyph: its screen rectangle (y0 above y1) and its texture
// coordinates (v0 above v1; the texture is stored upside down).
struct GlyphQuad {
    std::uint8_t code = 0;
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

struct StringLayout {
    std::vector<GlyphQuad> quads;
    float pen_x = 0.0f;
    float pen_y = 0.0f;
};

// FUN_00575980: lays out a string with the advance of the original. The pen
// starts at (x, y) with y down; 0x0a starts a row, 0x20 advances a space and
// every other byte places its glyph, or the glyph of ^ when the font has no
// such code, or advances a space when it has neither. The screen rectangle of
// a glyph is x0..x0 + width * scale and y - row_height..y.
StringLayout layout_string(const PixmapFont& font, std::uint32_t texture_width,
                           std::uint32_t texture_height, std::string_view text, float x,
                           float y, float row_height, float scale);

// The scale of a row of a UI text (FUN_0056e2b0): a row of `size` screen units
// with `aspect`, over the leading and base height of the font and the half
// height of the text space.
struct TextStyle {
    float x_scale = 1.0f;
    float y_scale = 1.0f;
};

TextStyle make_text_style(const PixmapFont& font, float size, float aspect, float half_height);

// The size of a string (FUN_0056df40), in screen units: the widest row over
// `half_width` and the sum of the row heights over `half_height`. 0x0a starts
// a row; a space or a byte without a glyph advances the space width.
struct TextSize {
    float width = 0.0f;
    float height = 0.0f;
};

TextSize measure_text(const PixmapFont& font, const TextStyle& style, std::string_view text,
                      float half_width, float half_height);

// Where a UI text starts (FUN_0056e300): the pen in the 640 x 480 space (y
// down, so the caller's y is up), the height of a row and the scale
// layout_string takes.
struct TextPen {
    float x = 0.0f;
    float y = 0.0f;
    float row_height = 0.0f;
    float scale = 0.0f;
};

TextPen text_pen(const PixmapFont& font, const TextStyle& style, float x, float y,
                 float half_width, float half_height);

}  // namespace bo2::engine
