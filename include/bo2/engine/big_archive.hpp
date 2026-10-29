#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bo2::engine {

// The original Spanish build forces this locale at boot: FUN_004aae40 calls
// Select("spanish") on the locale list and stores Locale=spanish in the
// config. The port targets that build, so it starts in the same locale.
inline constexpr const char* kDefaultLocale = "spanish";

// Language of the entries that exist once for every locale: name table index
// 0, `names()[0]` in every installed container.
inline constexpr const char* kDefaultLanguage = "default";

struct BigEntry {
    std::string type;
    std::string name;
    std::uint16_t language = 0;
    std::size_t offset = 0;
    std::size_t size = 0;
    std::vector<std::uint16_t> symbols;
};

class BigArchive {
public:
    static BigArchive load(const std::string& path);
    static BigArchive parse(std::vector<std::uint8_t> data);

    const std::vector<BigEntry>& entries() const { return entries_; }
    const std::vector<std::string>& names() const { return names_; }
    const std::vector<std::uint8_t>& bytes() const { return data_; }
    std::vector<std::uint8_t> extract(const BigEntry& entry) const;
    const BigEntry* find(const std::string& type, const std::string& name) const;

    // Name of the entry language: `names()[entry.language]`, or an empty
    // string when the index is outside the name table.
    std::string language_name(const BigEntry& entry) const;

    // Localized lookup. The engine loads an entry only when its language is
    // `default` or the current locale (FUN_006c8fb0); this returns the entry
    // of `locale` when there is one and the `default` copy otherwise, and
    // nullptr when the resource only exists in other languages. Use it for
    // the localized types (`strtable`, `lipsync`, `texture`); the two-argument
    // overload keeps returning the first match of any language.
    const BigEntry* find(const std::string& type, const std::string& name,
                         const std::string& locale) const;

private:
    std::vector<std::uint8_t> data_;
    std::vector<BigEntry> entries_;
    std::vector<std::string> names_;
};

bool iequals(const std::string& left, const std::string& right);
bool contains_ci(const std::string& haystack, const std::string& needle);

}  // namespace bo2::engine
