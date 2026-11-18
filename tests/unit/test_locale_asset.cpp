#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/dds.hpp"
#include "bo2/engine/pixmap_font.hpp"
#include "bo2/engine/string_table.hpp"
#include "bo2/engine/text.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

// Minimal SHA-256 (FIPS 180-4). The test compares digests instead of copying
// the original game text into the repository.
std::string sha256_hex(const std::string& text) {
    static const std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<std::uint8_t> data(text.begin(), text.end());
    const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * 8U;
    data.push_back(0x80);
    while (data.size() % 64 != 56) {
        data.push_back(0);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        data.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xFF));
    }
    const auto rotr = [](std::uint32_t value, unsigned amount) {
        return (value >> amount) | (value << (32U - amount));
    };
    for (std::size_t block = 0; block < data.size(); block += 64) {
        std::uint32_t w[64];
        for (std::size_t index = 0; index < 16; ++index) {
            const std::size_t at = block + index * 4;
            w[index] = (static_cast<std::uint32_t>(data[at]) << 24) |
                       (static_cast<std::uint32_t>(data[at + 1]) << 16) |
                       (static_cast<std::uint32_t>(data[at + 2]) << 8) |
                       static_cast<std::uint32_t>(data[at + 3]);
        }
        for (std::size_t index = 16; index < 64; ++index) {
            const std::uint32_t s0 =
                rotr(w[index - 15], 7) ^ rotr(w[index - 15], 18) ^ (w[index - 15] >> 3);
            const std::uint32_t s1 =
                rotr(w[index - 2], 17) ^ rotr(w[index - 2], 19) ^ (w[index - 2] >> 10);
            w[index] = w[index - 16] + s0 + w[index - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (std::size_t index = 0; index < 64; ++index) {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t choice = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + s1 + choice + k[index] + w[index];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + majority;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }
    static const char digits[] = "0123456789abcdef";
    std::string hex;
    for (const std::uint32_t word : h) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            hex.push_back(digits[(word >> shift) & 0xF]);
        }
    }
    return hex;
}

// Strict UTF-8 check: lead byte, continuation bytes, no overlong 2-byte form.
bool is_utf8(const std::string& text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index]);
        std::size_t extra = 0;
        if (lead < 0x80) {
            extra = 0;
        } else if (lead >= 0xC2 && lead < 0xE0) {
            extra = 1;
        } else if (lead >= 0xE0 && lead < 0xF0) {
            extra = 2;
        } else if (lead >= 0xF0 && lead < 0xF5) {
            extra = 3;
        } else {
            return false;
        }
        if (index + extra >= text.size()) {
            return false;  // truncated sequence
        }
        for (std::size_t offset = 1; offset <= extra; ++offset) {
            if ((static_cast<unsigned char>(text[index + offset]) & 0xC0) != 0x80) {
                return false;
            }
        }
        index += extra + 1;
    }
    return true;
}

bool has_non_ascii(const std::string& text) {
    for (const char character : text) {
        if (static_cast<unsigned char>(character) >= 0x80) {
            return true;
        }
    }
    return false;
}

// The font of the game (FUN_005df900): the entry, its texture and the glyphs
// the story text uses.
void test_game_font(const bo2::engine::BigArchive& archive) {
    const bo2::engine::BigEntry* entry = archive.find("texfont", "main/main");
    CHECK(entry != nullptr);
    if (entry == nullptr) {
        return;
    }
    const std::vector<std::uint8_t> data = archive.extract(*entry);
    CHECK(data.size() == 1780);
    const bo2::engine::PixmapFont font =
        bo2::engine::parse_pixmap_font(data, entry->symbols, archive.names());
    CHECK(font.texture == "fonts/revised");
    CHECK(std::fabs(font.base_height - 20.0f) < 1.0e-5f);
    CHECK(std::fabs(font.kerning - 2.0f) < 1.0e-5f);
    CHECK(std::fabs(font.leading - 5.0f) < 1.0e-5f);
    CHECK(std::fabs(font.space_width - 8.0f) < 1.0e-5f);
    CHECK(!font.upper_case_only);
    CHECK(font.glyphs.size() == 87);
    const auto same = [&font](std::uint8_t code, float x, float y, float width, float height) {
        const bo2::engine::FontGlyph* glyph = font.find(code);
        return glyph != nullptr && glyph->x == x && glyph->y == y && glyph->width == width &&
               glyph->height == height;
    };
    CHECK(same('a', 0.0f, 0.0f, 12.0f, 20.0f));
    CHECK(same('A', 105.0f, 105.0f, 12.0f, 20.0f));
    CHECK(same('T', 231.0f, 126.0f, 12.0f, 20.0f));
    CHECK(same('0', 42.0f, 42.0f, 13.0f, 20.0f));
    CHECK(same(0xc8, 105.0f, 84.0f, 20.0f, 20.0f));
    CHECK(same('S', 63.0f, 63.0f, 10.0f, 20.0f));
    CHECK(same('?', 63.0f, 63.0f, 10.0f, 20.0f));
    CHECK(font.find(' ') == nullptr);

    const bo2::engine::BigEntry* copy = archive.find("texfont", "main/main.fnt");
    CHECK(copy != nullptr);
    if (copy != nullptr) {
        CHECK(archive.extract(*copy) == data);
    }
}

// The upper-case codes each language's table uses, outside the control codes
// (the lone backslash included) and spaces, without the savedgame_ps2icon
// values. Only counts and letter sets: the game text stays out of the repo.
// FUN_005df900: the alpha shapes of the game font. The counts are the texels
// with alpha greater than 127 of each control glyph.
void test_game_font_glyphs(const bo2::engine::BigArchive& archive) {
    const bo2::engine::BigEntry* font_entry = archive.find("texfont", "main/main");
    const bo2::engine::BigEntry* texture_entry = archive.find("texture", "fonts/revised");
    CHECK(font_entry != nullptr);
    CHECK(texture_entry != nullptr);
    if (font_entry == nullptr || texture_entry == nullptr) {
        return;
    }
    const bo2::engine::PixmapFont font = bo2::engine::parse_pixmap_font(
        archive.extract(*font_entry), font_entry->symbols, archive.names());
    const bo2::engine::Image texture = bo2::engine::decode_dds(archive.extract(*texture_entry));
    CHECK(texture.width == 256);
    CHECK(texture.height == 256);
    const auto count = [&font, &texture](std::uint8_t code) {
        const bo2::engine::GlyphBitmap bitmap = bo2::engine::glyph_bitmap(font, texture, code);
        std::size_t total = 0;
        for (const std::uint8_t alpha : bitmap.alpha) {
            if (alpha > 127) {
                ++total;
            }
        }
        return total;
    };
    CHECK(count('a') == 76);
    CHECK(count('A') == 42);
    CHECK(count('T') == 66);
    CHECK(count('0') == 55);
    CHECK(count('?') == 35);
    CHECK(count('S') == 35);
    CHECK(count('.') == 4);
    CHECK(count('i') == 33);
    CHECK(count(0xc8) == 255);
    std::size_t total = 0;
    for (const bo2::engine::FontGlyph& glyph : font.glyphs) {
        total += count(glyph.code);
    }
    CHECK(total == 6917);
    const bo2::engine::GlyphBitmap a = bo2::engine::glyph_bitmap(font, texture, 'a');
    CHECK(a.width == 12);
    CHECK(a.height == 20);
    std::size_t first = 0;
    while (first < a.height) {
        bool any = false;
        for (std::uint32_t column = 0; column < a.width; ++column) {
            if (a.alpha[first * a.width + column] != 0) {
                any = true;
            }
        }
        if (any) {
            break;
        }
        ++first;
    }
    CHECK(first == 1);
}

// FUN_00575980: the three layout controls of the game font, with the texture
// of 256 x 256 and the named header values of BO2NX-119.
void test_layout_with_the_game_font(const bo2::engine::BigArchive& archive) {
    const bo2::engine::BigEntry* font_entry = archive.find("texfont", "main/main");
    const bo2::engine::BigEntry* texture_entry = archive.find("texture", "fonts/revised");
    CHECK(font_entry != nullptr);
    CHECK(texture_entry != nullptr);
    if (font_entry == nullptr || texture_entry == nullptr) {
        return;
    }
    const bo2::engine::PixmapFont font = bo2::engine::parse_pixmap_font(
        archive.extract(*font_entry), font_entry->symbols, archive.names());
    const bo2::engine::Image texture = bo2::engine::decode_dds(archive.extract(*texture_entry));
    CHECK(texture.width == 256);
    CHECK(texture.height == 256);
    const auto near = [](float left, float right) {
        return std::fabs(left - right) < 1.0e-4f;
    };
    {
        const bo2::engine::StringLayout layout = bo2::engine::layout_string(
            font, texture.width, texture.height, "ab 0", 100.0f, 200.0f, 20.0f, 1.0f);
        CHECK(layout.quads.size() == 3);
        CHECK(near(layout.pen_x, 152.0f));
        CHECK(near(layout.pen_y, 200.0f));
        if (layout.quads.size() == 3) {
            CHECK(near(layout.quads[0].x0, 100.0f));
            CHECK(near(layout.quads[0].x1, 112.0f));
            CHECK(near(layout.quads[0].y0, 180.0f));
            CHECK(near(layout.quads[0].y1, 200.0f));
            CHECK(near(layout.quads[0].u0, 0.0f));
            CHECK(near(layout.quads[0].u1, 0.046875f));
            CHECK(near(layout.quads[0].v0, 1.0f));
            CHECK(near(layout.quads[0].v1, 0.921875f));
            CHECK(near(layout.quads[1].x0, 114.0f));
            CHECK(near(layout.quads[1].x1, 127.0f));
            CHECK(near(layout.quads[2].x0, 137.0f));
            CHECK(near(layout.quads[2].x1, 150.0f));
            CHECK(near(layout.quads[2].u0, 0.1640625f));
            CHECK(near(layout.quads[2].u1, 0.21484375f));
            CHECK(near(layout.quads[2].v0, 0.8359375f));
            CHECK(near(layout.quads[2].v1, 0.7578125f));
        }
    }
    {
        const bo2::engine::StringLayout layout = bo2::engine::layout_string(
            font, texture.width, texture.height, "a\nb", 10.0f, 50.0f, 24.0f, 2.0f);
        CHECK(layout.quads.size() == 2);
        CHECK(near(layout.pen_x, 40.0f));
        CHECK(near(layout.pen_y, 74.0f));
        if (layout.quads.size() == 2) {
            CHECK(near(layout.quads[0].x0, 10.0f));
            CHECK(near(layout.quads[0].x1, 34.0f));
            CHECK(near(layout.quads[0].y0, 26.0f));
            CHECK(near(layout.quads[0].y1, 50.0f));
            CHECK(near(layout.quads[1].x0, 10.0f));
            CHECK(near(layout.quads[1].x1, 36.0f));
            CHECK(near(layout.quads[1].y0, 50.0f));
            CHECK(near(layout.quads[1].y1, 74.0f));
        }
    }
    {
        const bo2::engine::StringLayout layout = bo2::engine::layout_string(
            font, texture.width, texture.height, "a~b", 0.0f, 20.0f, 20.0f, 1.0f);
        CHECK(layout.quads.size() == 2);
        CHECK(near(layout.pen_x, 37.0f));
        if (layout.quads.size() == 2) {
            CHECK(near(layout.quads[0].x0, 0.0f));
            CHECK(near(layout.quads[0].x1, 12.0f));
            CHECK(near(layout.quads[1].x0, 22.0f));
            CHECK(near(layout.quads[1].x1, 35.0f));
            CHECK(near(layout.quads[0].y0, 0.0f));
            CHECK(near(layout.quads[0].y1, 20.0f));
        }
    }
}

// FUN_00772320 over every value of the ui/UI table of the five languages:
// the number of values, rows, glyphs, controls and warnings. The 21 control
// items are value 4 and the single Spanish warning is an unsupported
// character.
void test_text_parser_on_the_menu_table(const bo2::engine::BigArchive& archive) {
    struct Case {
        const char* locale;
        std::size_t values;
        std::size_t rows;
        std::size_t glyphs;
        std::size_t controls;
        std::size_t warnings;
    };
    const Case cases[] = {
        {"english", 137, 165, 1905, 21, 0}, {"french", 137, 165, 2239, 21, 0},
        {"italian", 135, 163, 2142, 21, 0}, {"german", 137, 165, 2144, 21, 0},
        {"spanish", 137, 165, 2295, 21, 1},
    };
    for (const Case& item : cases) {
        const bo2::engine::BigEntry* entry =
            archive.find("strtable", "ui/UI", item.locale);
        CHECK(entry != nullptr);
        if (entry == nullptr) {
            continue;
        }
        const bo2::engine::StringTable table =
            bo2::engine::StringTable::parse(archive, *entry);
        std::size_t rows = 0;
        std::size_t glyphs = 0;
        std::size_t controls = 0;
        std::size_t warnings = 0;
        std::size_t wrong_control = 0;
        std::size_t unsupported = 0;
        for (const bo2::engine::StringTableRecord& record : table.records()) {
            const bo2::engine::ParsedText parsed = bo2::engine::parse_text(record.value);
            rows += parsed.rows.size();
            warnings += parsed.warnings.size();
            for (const std::vector<bo2::engine::TextItem>& row : parsed.rows) {
                for (const bo2::engine::TextItem& text_item : row) {
                    if (text_item.control) {
                        ++controls;
                        if (text_item.value != 4) {
                            ++wrong_control;
                        }
                    } else {
                        ++glyphs;
                    }
                }
            }
            for (const bo2::engine::TextWarning warning : parsed.warnings) {
                if (warning == bo2::engine::TextWarning::UnsupportedCharacter) {
                    ++unsupported;
                }
            }
        }
        CHECK(table.records().size() == item.values);
        CHECK(rows == item.rows);
        CHECK(glyphs == item.glyphs);
        CHECK(controls == item.controls);
        CHECK(warnings == item.warnings);
        CHECK(wrong_control == 0);
        CHECK(unsupported == item.warnings);
    }
}

void test_marker_use_per_language(const bo2::engine::BigArchive& archive) {
    struct Expected {
        const char* locale;
        const char* letters;
        int count;
        std::size_t bytes;
    };
    const Expected expected[] = {
        {"english", "", 0, 1692},      {"french", "CFHJLQ", 38, 1999},
        {"italian", "FM", 9, 1906},    {"german", "BEO", 18, 1944},
        {"spanish", "CTUVWXY", 37, 2053},
    };
    for (const Expected& item : expected) {
        const bo2::engine::BigEntry* entry = archive.find("strtable", "ui/UI", item.locale);
        CHECK(entry != nullptr);
        if (entry == nullptr) {
            continue;
        }
        const bo2::engine::StringTable table = bo2::engine::StringTable::parse(archive, *entry);
        std::string used;
        std::size_t bytes = 0;
        int count = 0;
        for (const bo2::engine::StringTableRecord& record : table.records()) {
            if (record.key.rfind("savedgame_ps2icon_", 0) == 0) {
                continue;
            }
            std::string text = record.value;
            for (const std::string& code : {std::string("@c"), std::string("@n"),
                                            std::string("%d"), std::string("\\")}) {
                std::size_t at = 0;
                while ((at = text.find(code, at)) != std::string::npos) {
                    text.erase(at, code.size());
                }
            }
            text.erase(std::remove(text.begin(), text.end(), ' '), text.end());
            bytes += text.size();
            for (const char character : text) {
                if (character >= 'A' && character <= 'Z') {
                    ++count;
                    if (used.find(character) == std::string::npos) {
                        used.push_back(character);
                    }
                }
            }
        }
        std::sort(used.begin(), used.end());
        CHECK(used == item.letters);
        CHECK(count == item.count);
        CHECK(bytes == item.bytes);
    }
}

// FUN_007556e0 and FUN_00755970 over the memory card prompts of ui/UI in the
// five languages. The expected text is not copied into the repository: the
// checks use its size and its SHA-256 digest.
void test_memory_card_prompts(const bo2::engine::BigArchive& archive) {
    const char* const locales[] = {"english", "french", "italian", "german", "spanish"};
    for (const char* locale : locales) {
        const bo2::engine::BigEntry* entry = archive.find("strtable", "ui/UI", locale);
        CHECK(entry != nullptr);
        if (entry == nullptr) {
            continue;
        }
        const bo2::engine::StringTable table =
            bo2::engine::StringTable::parse(archive, *entry);
        std::vector<std::string> warnings;
        const std::string absent =
            bo2::engine::key_text(table, "ui/UI", "card_absent_menu_prompt", &warnings);
        CHECK(warnings.empty());
        const std::string absent_text = bo2::engine::format_count(absent, 45);
        CHECK(absent_text.size() == 93);
        CHECK(sha256_hex(absent_text) ==
              "99823f6fc5d3d230e58407a8861f31bdfdb642d5342f5ff4e38fd3e0eb93e8cd");
        const std::string full =
            bo2::engine::key_text(table, "ui/UI", "card_full_menu_prompt", &warnings);
        CHECK(warnings.empty());
        const std::string full_text = bo2::engine::format_count(full, 45);
        CHECK(full_text.size() == 137);
        CHECK(sha256_hex(full_text) ==
              "d7043281c734aca7b8fbf0d1ffb83f0565f434047d99a4845372d7f21f32d735");
        std::size_t with_code = 0;
        for (const bo2::engine::StringTableRecord& record : table.records()) {
            if (record.value.find("%d") != std::string::npos) {
                ++with_code;
            }
        }
        CHECK(with_code == 2);
    }
}

}  // namespace

int main() {
    // Known answer for the digest helper, so a skipped asset run still checks it.
    CHECK(sha256_hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256_hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    const char* root = std::getenv("BO2NX_ASSETS_DIR");
    if (root == nullptr) {
        std::cout << "locale asset test skipped (BO2NX_ASSETS_DIR is unset)\n";
        return 0;
    }
    const std::filesystem::path path =
        std::filesystem::path(root) / "media/gogames/bo2/code.big";
    if (!std::filesystem::exists(path)) {
        std::cout << "locale asset test skipped (code.big not found)\n";
        return 0;
    }
    try {
        const bo2::engine::BigArchive archive = bo2::engine::BigArchive::load(path.string());
        test_game_font(archive);
        test_game_font_glyphs(archive);
        test_layout_with_the_game_font(archive);
        test_text_parser_on_the_menu_table(archive);
        test_memory_card_prompts(archive);
        test_marker_use_per_language(archive);
        // Every installed container starts its name table with the language names.
        const std::vector<std::string> expected = {"default", "english", "french",
                                                   "italian", "german",  "spanish"};
        CHECK(archive.names().size() > expected.size());
        for (std::size_t index = 0; index < expected.size() && index < archive.names().size();
             ++index) {
            CHECK(archive.names()[index] == expected[index]);
        }

        // ui/UI exists once per language; the port locale picks the Spanish copy.
        const struct {
            const char* locale;
            std::uint16_t language;
        } cases[] = {{"spanish", 5}, {"english", 1}, {"french", 2}, {"italian", 3},
                     {"german", 4},  {"default", 0}, {"klingon", 0}};
        for (const auto& item : cases) {
            const bo2::engine::BigEntry* entry = archive.find("strtable", "ui/UI", item.locale);
            CHECK(entry != nullptr);
            if (entry != nullptr) {
                CHECK(entry->language == item.language);
            }
        }
        const bo2::engine::BigEntry* port =
            archive.find("strtable", "ui/UI", bo2::engine::kDefaultLocale);
        CHECK(port != nullptr && archive.language_name(*port) == "spanish");

        // The Spanish table: 137 uniform records. The expected values are not
        // copied into the repository; they are the SHA-256 digests of the stored
        // Spanish values of two keys, computed from the installed tree.
        std::string spanish_start;
        if (port != nullptr) {
            const bo2::engine::StringTable table = bo2::engine::StringTable::parse(archive, *port);
            CHECK(table.records().size() == 137);
            CHECK(table.irregular().empty());
            const std::string* start = table.find("start_game_menu_item_name");
            CHECK(start != nullptr);
            if (start != nullptr) {
                spanish_start = *start;
                CHECK(sha256_hex(*start) ==
                      "35a4352b669e11e4a15000bdfdb1a3441fb0933c1818fa28f44a4e6cc587d508");
            }
            const std::string* credits = table.find("credits_menu_item_name");
            CHECK(credits != nullptr);
            if (credits != nullptr) {
                CHECK(sha256_hex(*credits) ==
                      "4756995ad5f641e7bdcc03c2b8e05aeee9a7f1d1d74ed64d523085f419f85e39");
                // This stored value is ASCII; the displayed text carries the accent.
                CHECK(!has_non_ascii(*credits));
                const std::string shown =
                    bo2::engine::display_text(*credits, bo2::engine::kDefaultLocale);
                CHECK(has_non_ascii(shown));
                CHECK(shown.size() > credits->size());
            }
            // One Spanish value stores a raw Latin-1 byte; every displayed value is UTF-8.
            std::size_t raw_latin1 = 0;
            for (const bo2::engine::StringTableRecord& record : table.records()) {
                raw_latin1 += has_non_ascii(record.value) ? 1 : 0;
                CHECK(is_utf8(bo2::engine::display_text(record.value, bo2::engine::kDefaultLocale)));
            }
            CHECK(raw_latin1 == 1);
        }

        // The default copy is the untranslated table; the Italian one has the irregular record.
        const bo2::engine::BigEntry* untranslated = archive.find("strtable", "ui/UI", "default");
        if (untranslated != nullptr) {
            const bo2::engine::StringTable table =
                bo2::engine::StringTable::parse(archive, *untranslated);
            const std::string* start = table.find("start_game_menu_item_name");
            CHECK(start != nullptr && *start != spanish_start);
        }
        const bo2::engine::BigEntry* italian = archive.find("strtable", "ui/UI", "italian");
        if (italian != nullptr) {
            const bo2::engine::StringTable table = bo2::engine::StringTable::parse(archive, *italian);
            CHECK(table.irregular().size() == 1);
        }
    } catch (const std::exception& error) {
        std::cerr << "locale asset test failed: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "locale asset tests passed\n";
    return 0;
}
