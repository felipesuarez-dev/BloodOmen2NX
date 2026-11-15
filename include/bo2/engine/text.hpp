#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace bo2::engine {

// One item of a parsed row: a glyph code or a control value.
struct TextItem {
    bool control = false;
    std::uint8_t value = 0;
};

// The warnings of FUN_00772320 (docs/TEXT.md, "Parsing a string").
enum class TextWarning {
    UnsupportedCharacter,
    MultipleSpaces,
    LeadingSpace,
    TrailingSpaces,
    Alignment,
    MultipleNewlines,
    ReservedCommand,
    UnsupportedCommand,
    EndsWithinCommand,
    TrailingNewline,
};

struct ParsedText {
    std::vector<std::vector<TextItem>> rows;
    std::vector<TextWarning> warnings;
};

// FUN_00772320: parses the bytes of a stored text into rows of glyph and
// control items and reports the warnings of the original. The parse does not
// look at the locale; the caller passes the raw strtable value.
ParsedText parse_text(std::string_view stored);

}  // namespace bo2::engine
