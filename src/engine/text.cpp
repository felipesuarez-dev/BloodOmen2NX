#include "bo2/engine/text.hpp"

namespace bo2::engine {

namespace {

// The glyph of a `\` command from the table of docs/TEXT.md, or -1 when the
// command is not supported.
int backslash_glyph(std::uint8_t character) {
    switch (character) {
        case 'q':
        case 'y':
            return 0xC8;
        case 'b':
        case 'g':
            return 0xC9;
        case 'a':
        case 'c':
            return 0xCA;
        case 'x':
            return 0xCB;
        case '1':
            return 0xCC;
        case '2':
            return 0xCD;
        case '3':
            return 0xCE;
        case '4':
            return 0xCF;
        case 'u':
            return 0xD0;
        case 'd':
            return 0xD1;
        case 'h':
            return 0xD2;
        case 'l':
            return 0xD3;
        case '[':
            return 0xD4;
        case ']':
            return 0xD5;
        case '*':
            return 0xD6;
        case '#':
            return 0xD7;
        case ' ':
            return 0x20;
        case '\\':
            return 0x5C;
        default:
            return -1;
    }
}

}  // namespace

// FUN_00772320: the parser keeps one row that starts empty, a flag for the
// last item being a space and a flag for the row having just started. `@` and
// `\` open a one-character command; a printable ASCII character is a glyph
// and a space is a glyph with its own warnings.
ParsedText parse_text(std::string_view stored) {
    ParsedText parsed;
    parsed.rows.emplace_back();
    bool last_space = false;
    bool row_start = true;
    int pending = 0;

    const auto add_glyph = [&parsed, &last_space, &row_start](std::uint8_t code,
                                                              bool is_space) {
        parsed.rows.back().push_back(TextItem{false, code});
        last_space = is_space;
        row_start = false;
    };
    const auto new_row = [&parsed, &last_space, &row_start](bool control) {
        if (last_space) {
            parsed.warnings.push_back(TextWarning::TrailingSpaces);
        }
        if (row_start) {
            parsed.warnings.push_back(TextWarning::MultipleNewlines);
        }
        parsed.rows.emplace_back();
        if (control) {
            parsed.rows.back().push_back(TextItem{true, 1});
        }
        last_space = false;
        row_start = true;
    };

    for (const char raw : stored) {
        const std::uint8_t character = static_cast<std::uint8_t>(raw);
        if (pending == '@') {
            pending = 0;
            switch (character) {
                case '@':
                    add_glyph('@', false);
                    break;
                case 'c':
                case 'l':
                case 'r':
                    if (row_start) {
                        const std::uint8_t value =
                            (character == 'c') ? 4 : ((character == 'l') ? 2 : 3);
                        parsed.rows.back().push_back(TextItem{true, value});
                        last_space = false;
                    } else {
                        parsed.warnings.push_back(TextWarning::Alignment);
                        last_space = false;
                    }
                    break;
                case 'n':
                    new_row(false);
                    break;
                case 'p':
                    new_row(true);
                    break;
                case 'w':
                    parsed.warnings.push_back(TextWarning::ReservedCommand);
                    last_space = false;
                    break;
                default:
                    // An unknown @ command does nothing and keeps both flags.
                    break;
            }
            continue;
        }
        if (pending == '\\') {
            pending = 0;
            if (character == 'n') {
                new_row(false);
                continue;
            }
            const int glyph = backslash_glyph(character);
            if (glyph >= 0) {
                add_glyph(static_cast<std::uint8_t>(glyph), false);
            } else {
                parsed.warnings.push_back(TextWarning::UnsupportedCommand);
                last_space = false;
                row_start = false;
            }
            continue;
        }
        if (character == '@' || character == '\\') {
            pending = character;
            continue;
        }
        if (character == ' ') {
            if (last_space) {
                parsed.warnings.push_back(TextWarning::MultipleSpaces);
            }
            if (row_start) {
                parsed.warnings.push_back(TextWarning::LeadingSpace);
            }
            add_glyph(' ', true);
            continue;
        }
        if (character >= 0x21 && character <= 0x7E) {
            add_glyph(character, false);
            continue;
        }
        parsed.warnings.push_back(TextWarning::UnsupportedCharacter);
        last_space = false;
        row_start = false;
    }

    if (pending != 0) {
        parsed.warnings.push_back(TextWarning::EndsWithinCommand);
    }
    if (last_space) {
        parsed.warnings.push_back(TextWarning::TrailingSpaces);
    }
    if (row_start) {
        parsed.warnings.push_back(TextWarning::TrailingNewline);
    }
    return parsed;
}

}  // namespace bo2::engine
