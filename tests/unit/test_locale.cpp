#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "bo2/engine/big_archive.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

// Synthetic container builder. Unlike the helper in test_engine.cpp it takes
// the name table and a language per entry, which is what the locale rules need.
struct Item {
    std::string type;
    std::uint16_t name_index = 0;
    std::uint16_t language = 0;
    std::vector<std::uint8_t> payload;
    std::vector<std::uint16_t> symbols;
};

void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int index = 0; index < 4; ++index) {
        out.push_back(static_cast<std::uint8_t>((value >> (8 * index)) & 0xFF));
    }
}

void patch32(std::vector<std::uint8_t>& out, std::size_t offset, std::uint32_t value) {
    for (int index = 0; index < 4; ++index) {
        out[offset + static_cast<std::size_t>(index)] =
            static_cast<std::uint8_t>((value >> (8 * index)) & 0xFF);
    }
}

void append(std::vector<std::uint8_t>& out, const std::string& text) {
    out.insert(out.end(), text.begin(), text.end());
}

std::vector<std::uint8_t> build_big(const std::vector<std::string>& names,
                                    const std::vector<Item>& items) {
    std::vector<std::uint8_t> directory;
    append(directory, "symlist");
    directory.push_back(0);
    put32(directory, 0);
    put32(directory, 0);
    put32(directory, static_cast<std::uint32_t>(names.size()));
    for (const std::string& name : names) {
        append(directory, name);
        directory.push_back(0);
    }
    if ((directory.size() & 1U) != 0U) {
        directory.push_back(0);
    }
    patch32(directory, 8, static_cast<std::uint32_t>(directory.size()));

    std::vector<std::uint8_t> out;
    append(out, "goefile");
    out.push_back(0);
    put32(out, 0);
    put32(out, 1);
    out.insert(out.end(), directory.begin(), directory.end());

    for (const Item& item : items) {
        std::vector<std::uint8_t> body;
        const std::size_t symbols_size = (20 + item.symbols.size() * 2 + 3) & ~std::size_t{3U};
        append(body, "symlist");
        body.push_back(0);
        put32(body, static_cast<std::uint32_t>(symbols_size));
        put32(body, 0);
        put32(body, static_cast<std::uint32_t>(item.symbols.size()));
        for (std::uint16_t symbol : item.symbols) {
            put16(body, symbol);
        }
        while (body.size() < symbols_size) {
            body.push_back(0);
        }
        put32(body, static_cast<std::uint32_t>(item.payload.size()));
        body.insert(body.end(), item.payload.begin(), item.payload.end());
        std::size_t entry_length = 20 + body.size();
        while ((entry_length & 3U) != 0U) {
            body.push_back(0);
            ++entry_length;
        }
        std::string type = item.type;
        type.resize(8, '\0');
        append(out, type);
        put32(out, static_cast<std::uint32_t>(entry_length));
        put32(out, 0);
        put16(out, item.name_index);
        put16(out, item.language);
        out.insert(out.end(), body.begin(), body.end());
    }
    patch32(out, 8, static_cast<std::uint32_t>(out.size()));
    return out;
}

// Name table prefix shared by the installed containers; indices 0..5 are the
// language numbers 0 = default ... 5 = spanish.
std::vector<std::string> language_names() {
    return {"default", "english", "french", "italian", "german", "spanish"};
}

std::vector<std::string> names_with(const std::vector<std::string>& extra) {
    std::vector<std::string> names = language_names();
    names.insert(names.end(), extra.begin(), extra.end());
    return names;
}

std::uint8_t first_byte(const bo2::engine::BigArchive& archive, const bo2::engine::BigEntry* entry) {
    return archive.extract(*entry).at(0);
}

void test_language_names() {
    const auto archive = bo2::engine::BigArchive::parse(build_big(
        names_with({"ui/ui"}), {{"strtable", 6, 5, {5}, {}}, {"strtable", 6, 99, {9}, {}}}));
    CHECK(archive.language_name(archive.entries()[0]) == "spanish");
    // An index outside the name table has no name.
    CHECK(archive.language_name(archive.entries()[1]).empty());
}

void test_locale_wins_over_default() {
    // The file order of code.big: languages 1..5 first, the default copy last.
    const auto archive = bo2::engine::BigArchive::parse(build_big(
        names_with({"ui/ui"}),
        {
            {"strtable", 6, 1, {1}, {}},
            {"strtable", 6, 2, {2}, {}},
            {"strtable", 6, 5, {5}, {}},
            {"strtable", 6, 0, {0}, {}},
        }));
    const bo2::engine::BigEntry* spanish = archive.find("strtable", "ui/ui", "spanish");
    CHECK(spanish != nullptr);
    CHECK(spanish != nullptr && spanish->language == 5);
    CHECK(spanish != nullptr && first_byte(archive, spanish) == 5);
    // Case-insensitive on type, name and locale, like the two-argument overload.
    CHECK(archive.find("STRTABLE", "UI/UI", "SPANISH") == spanish);

    const bo2::engine::BigEntry* french = archive.find("strtable", "ui/ui", "french");
    CHECK(french != nullptr && french->language == 2);
    const bo2::engine::BigEntry* english = archive.find("strtable", "ui/ui", "english");
    CHECK(english != nullptr && english->language == 1);

    // The port default is Spanish.
    CHECK(std::string(bo2::engine::kDefaultLocale) == "spanish");
    CHECK(archive.find("strtable", "ui/ui", bo2::engine::kDefaultLocale) == spanish);
}

void test_default_is_the_fallback() {
    // German has no copy of this resource: the default copy answers, even when
    // it is listed after entries of other languages.
    const auto archive = bo2::engine::BigArchive::parse(build_big(
        names_with({"vo/line"}),
        {
            {"lipsync", 6, 2, {2}, {}},
            {"lipsync", 6, 0, {0}, {}},
            {"lipsync", 6, 5, {5}, {}},
        }));
    const bo2::engine::BigEntry* german = archive.find("lipsync", "vo/line", "german");
    CHECK(german != nullptr && german->language == 0);
    // Asking for the default language itself returns the default copy.
    const bo2::engine::BigEntry* plain = archive.find("lipsync", "vo/line", "default");
    CHECK(plain != nullptr && plain->language == 0);
    // An unknown locale falls back too.
    const bo2::engine::BigEntry* unknown = archive.find("lipsync", "vo/line", "klingon");
    CHECK(unknown != nullptr && unknown->language == 0);
}

void test_other_languages_are_skipped() {
    // Only a French copy exists: the engine skips it for a Spanish session
    // (FUN_006c8fb0), so the lookup finds nothing.
    const auto archive = bo2::engine::BigArchive::parse(
        build_big(names_with({"hud/load"}), {{"texture", 6, 2, {2}, {}}}));
    CHECK(archive.find("texture", "hud/load", "spanish") == nullptr);
    CHECK(archive.find("texture", "hud/load", "french") != nullptr);
    // The two-argument overload is untouched: first match of any language.
    CHECK(archive.find("texture", "hud/load") != nullptr);
    CHECK(archive.find("texture", "missing", "spanish") == nullptr);
}

void test_plain_lookup_is_unchanged() {
    // Non-localized resources have language 0; both overloads return them, and
    // the two-argument overload still returns the first duplicate.
    const auto archive = bo2::engine::BigArchive::parse(build_big(
        names_with({"alpha", "ui/ui"}),
        {
            {"model", 6, 0, {1}, {}},
            {"strtable", 7, 1, {10}, {}},
            {"strtable", 7, 5, {50}, {}},
        }));
    const bo2::engine::BigEntry* model = archive.find("model", "alpha");
    CHECK(model != nullptr);
    CHECK(archive.find("model", "alpha", "spanish") == model);
    const bo2::engine::BigEntry* first = archive.find("strtable", "ui/ui");
    CHECK(first != nullptr && first->language == 1);
    const bo2::engine::BigEntry* localized = archive.find("strtable", "ui/ui", "spanish");
    CHECK(localized != nullptr && localized->language == 5);
}

}  // namespace

int main() {
    try {
        test_language_names();
        test_locale_wins_over_default();
        test_default_is_the_fallback();
        test_other_languages_are_skipped();
        test_plain_lookup_is_unchanged();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "locale tests passed\n";
    return 0;
}
