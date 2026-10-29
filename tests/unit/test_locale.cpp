#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "bo2/common/reader.hpp"
#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/string_table.hpp"

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

template <typename Function>
bool throws_parse_error(Function&& function) {
    try {
        function();
    } catch (const bo2::common::ParseError&) {
        return true;
    }
    return false;
}

// strtable payload: "default\0", payload size, 1, 2, then the u16 words.
std::vector<std::uint8_t> strtable_payload(const std::vector<std::uint16_t>& words) {
    std::vector<std::uint8_t> out;
    append(out, "default");
    out.push_back(0);
    put32(out, static_cast<std::uint32_t>(20 + words.size() * 2));
    put32(out, 1);
    put32(out, 2);
    for (std::uint16_t word : words) {
        put16(out, word);
    }
    return out;
}

constexpr std::uint16_t kEnd = 0xFFFF;

// Names 6.. of the synthetic table container.
std::vector<std::string> strtable_names() {
    return names_with({"ui/ui", "key_one", "uno", "key_two", "TCUVWXY", "one"});
}

bo2::engine::StringTable parse_table(const std::vector<std::uint16_t>& words,
                                     const std::vector<std::uint16_t>& symbols) {
    const auto archive = bo2::engine::BigArchive::parse(
        build_big(strtable_names(), {{"strtable", 6, 5, strtable_payload(words), symbols}}));
    return bo2::engine::StringTable::parse(archive, archive.entries()[0]);
}

void test_string_table_records() {
    // symbols: ref0 key_one, ref1 uno, ref2 key_two, ref3 TCUVWXY.
    const bo2::engine::StringTable table =
        parse_table({7, 0, 1, kEnd, 7, 2, 3, kEnd}, {7, 8, 9, 10});
    CHECK(table.records().size() == 2);
    CHECK(table.irregular().empty());
    CHECK(table.records()[0].key == "key_one");
    CHECK(table.records()[0].value == "uno");
    CHECK(table.records()[1].key == "key_two");
    const std::string* value = table.find("KEY_ONE");
    CHECK(value != nullptr && *value == "uno");
    CHECK(table.find("missing") == nullptr);
    // Different keys may share a value symbol.
    const bo2::engine::StringTable shared =
        parse_table({7, 0, 1, kEnd, 7, 2, 1, kEnd}, {7, 8, 9, 10});
    CHECK(shared.records().size() == 2);
    CHECK(shared.records()[1].value == "uno");
}

void test_string_table_irregular_record() {
    // The shape seen once in the Italian ui/UI: a record that is not
    // [7, key, value] up to its terminator. It is kept raw, skipped by
    // find(), and the records around it keep their alignment.
    const bo2::engine::StringTable table = parse_table(
        {7, 0, 1, kEnd, 0x0107, 2, 3, 0, 1, 3, 7, 0, 3, kEnd, 7, 2, 3, kEnd}, {7, 8, 9, 10});
    CHECK(table.records().size() == 2);
    CHECK(table.records()[0].key == "key_one");
    CHECK(table.records()[1].key == "key_two");
    CHECK(table.records()[1].value == "TCUVWXY");
    CHECK(table.irregular().size() == 1);
    CHECK(table.irregular()[0] ==
          std::vector<std::uint16_t>({0x0107, 2, 3, 0, 1, 3, 7, 0, 3}));
    // A lone terminator is an empty irregular record.
    const bo2::engine::StringTable empty = parse_table({kEnd, 7, 0, 1, kEnd}, {7, 8});
    CHECK(empty.records().size() == 1);
    CHECK(empty.irregular().size() == 1 && empty.irregular()[0].empty());
}

void test_string_table_rejects_bad_payloads() {
    const std::vector<std::uint16_t> symbols = {7, 8};
    // A reference outside the symbol list, a symbol outside the name table.
    CHECK(throws_parse_error([&] { parse_table({7, 0, 2, kEnd}, symbols); }));
    CHECK(throws_parse_error([&] { parse_table({7, 0, 1, kEnd}, {7, 500}); }));
    // No terminator on the last record.
    CHECK(throws_parse_error([&] { parse_table({7, 0, 1}, symbols); }));

    const auto archive = bo2::engine::BigArchive::parse(build_big(
        strtable_names(), {{"strtable", 6, 5, strtable_payload({7, 0, 1, kEnd}), symbols}}));
    const bo2::engine::BigEntry good = archive.entries()[0];
    CHECK(bo2::engine::StringTable::parse(archive, good).records().size() == 1);

    // Size field, magic and odd payload length are checked.
    auto corrupt = [&](std::size_t offset, std::uint8_t value) {
        std::vector<std::uint8_t> bytes = archive.bytes();
        bytes[good.offset + offset] = value;
        const auto changed = bo2::engine::BigArchive::parse(bytes);
        return throws_parse_error(
            [&] { bo2::engine::StringTable::parse(changed, changed.entries()[0]); });
    };
    CHECK(corrupt(0, 'x'));
    CHECK(corrupt(8, 0x7F));
    // An odd payload length whose size field is consistent.
    std::vector<std::uint8_t> odd_payload = strtable_payload({7, 0, 1, kEnd});
    odd_payload.push_back(0);
    patch32(odd_payload, 8, static_cast<std::uint32_t>(odd_payload.size()));
    const auto odd = bo2::engine::BigArchive::parse(
        build_big(strtable_names(), {{"strtable", 6, 5, odd_payload, symbols}}));
    CHECK(throws_parse_error([&] { bo2::engine::StringTable::parse(odd, odd.entries()[0]); }));
    // Shorter than the header.
    bo2::engine::BigEntry tiny = good;
    tiny.size = 12;
    CHECK(throws_parse_error([&] { bo2::engine::StringTable::parse(archive, tiny); }));
}

void test_string_table_by_locale() {
    // Spanish and default copies of the same table, as in code.big.
    const auto archive = bo2::engine::BigArchive::parse(build_big(
        strtable_names(),
        {
            {"strtable", 6, 5, strtable_payload({7, 0, 1, kEnd}), {7, 8}},
            {"strtable", 6, 0, strtable_payload({7, 0, 1, kEnd}), {7, 11}},
        }));
    const bo2::engine::BigEntry* spanish =
        archive.find("strtable", "ui/ui", bo2::engine::kDefaultLocale);
    CHECK(spanish != nullptr);
    if (spanish != nullptr) {
        const bo2::engine::StringTable table = bo2::engine::StringTable::parse(archive, *spanish);
        const std::string* value = table.find("key_one");
        CHECK(value != nullptr && *value == "uno");
    }
    const bo2::engine::BigEntry* german = archive.find("strtable", "ui/ui", "german");
    CHECK(german != nullptr);
    if (german != nullptr) {
        const bo2::engine::StringTable table = bo2::engine::StringTable::parse(archive, *german);
        const std::string* value = table.find("key_one");
        CHECK(value != nullptr && *value == "one");
    }
}

void test_display_text() {
    // Inferred mapping: T C U V W X Y -> a e i o u n-tilde inverted-question (UTF-8).
    CHECK(bo2::engine::display_text("TCUVWXY", "spanish") ==
          "\xC3\xA1\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA\xC3\xB1\xC2\xBF");
    CHECK(bo2::engine::display_text("TCUVWXY", "SPANISH") ==
          bo2::engine::display_text("TCUVWXY", "spanish"));
    // Lower case, digits, punctuation and the lower-case control codes are untouched.
    CHECK(bo2::engine::display_text("@c\\x %d: a-z (1).?", "spanish") == "@c\\x %d: a-z (1).?");
    CHECK(bo2::engine::display_text("", "spanish").empty());
    // Locales without a mapping return the stored text.
    CHECK(bo2::engine::display_text("TCUVWXY", "english") == "TCUVWXY");
    CHECK(bo2::engine::display_text("TCUVWXY", "default") == "TCUVWXY");
}

}  // namespace

int main() {
    try {
        test_language_names();
        test_locale_wins_over_default();
        test_default_is_the_fallback();
        test_other_languages_are_skipped();
        test_plain_lookup_is_unchanged();
        test_string_table_records();
        test_string_table_irregular_record();
        test_string_table_rejects_bad_payloads();
        test_string_table_by_locale();
        test_display_text();
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
