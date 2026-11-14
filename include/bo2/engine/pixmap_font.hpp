#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

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
// header floats in file order (their meaning is not read), the upper-case-only
// flag and the glyphs. Loader FUN_005df900.
struct PixmapFont {
    std::string texture;
    std::array<float, 4> header{};
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

}  // namespace bo2::engine
