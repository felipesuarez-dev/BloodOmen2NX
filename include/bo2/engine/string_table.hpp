#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bo2/engine/big_archive.hpp"

namespace bo2::engine {

// One `strtable` record: both strings are names of the container name table.
// `value` is the stored text: lower case, with upper-case letters standing for
// accented glyphs of the game font (see display_text).
struct StringTableRecord {
    std::string key;
    std::string value;
};

// `strtable` resource (StringTableManager, get-by-name FUN_006a90c0). The
// payload is a 20-byte header followed by u16 records
// `[7, key_ref, value_ref, 0xffff]`; the refs index the entry's symbol list
// and each symbol indexes the container name table. See
// docs/FORMATS/strtable.md.
class StringTable {
public:
    // Parses `entry`, which must belong to `archive`. Throws
    // common::ParseError on a bad header, a truncated payload or a reference
    // outside the symbol list or name table.
    static StringTable parse(const BigArchive& archive, const BigEntry& entry);

    const std::vector<StringTableRecord>& records() const { return records_; }

    // Records that do not have the `[7, key, value]` shape, as raw words
    // without the terminator. They are not decoded and are not searchable.
    const std::vector<std::vector<std::uint16_t>>& irregular() const { return irregular_; }

    // Stored text of `key` (case-insensitive), or nullptr.
    const std::string* find(const std::string& key) const;

private:
    std::vector<StringTableRecord> records_;
    std::vector<std::vector<std::uint16_t>> irregular_;
};

// Maps the stored text to UTF-8 for display. INFERRED, not decoded: the
// glyph art of `fonts/revised` was not read, the letters come from word
// evidence over every Spanish record (the only upper-case letters used
// outside control codes are T C U V W X Y). For `spanish`:
// T=a-acute C=e-acute U=i-acute V=o-acute W=u-acute X=n-tilde Y=inverted
// question mark. Other locales have no table yet and return the text as is.
// Control codes (`@c`, `@n`, `\x`, `\q`, `%d`) are lower case and pass through.
std::string display_text(const std::string& stored, const std::string& locale);

}  // namespace bo2::engine
