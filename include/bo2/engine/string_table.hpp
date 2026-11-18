#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bo2/engine/big_archive.hpp"

namespace bo2::engine {

// One `strtable` record: both strings are names of the container name table.
// `value` is the stored text: lower case, with upper-case letters standing for
// accented glyphs of the game font and a few raw Latin-1 bytes (see
// display_text).
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

// Maps the stored text to UTF-8 for display, in every locale. The table comes
// from the glyph art of `fonts/revised` (docs/FORMATS/strtable.md): each
// upper-case code is an accented capital, and display_text returns the
// lower-case letter: A=c-cedilla B=u-diaeresis C=e-acute D=a-circumflex
// E=a-diaeresis F=a-grave G=sharp-s H=e-circumflex I=e-diaeresis J=e-grave
// K=i-diaeresis L=i-circumflex M=i-grave N=o-circumflex O=o-diaeresis
// P=o-grave Q=u-circumflex R=u-grave S=question-mark T=a-acute U=i-acute
// V=o-acute W=u-acute X=n-tilde Y=inverted-question Z=inverted-exclamation.
// Raw Latin-1 bytes (0x80-0xFF) become their UTF-8 form.
// Control codes (`@c`, `@n`, `\x`, `\q`, `%d`) are lower case and pass through.
std::string display_text(const std::string& stored, const std::string& locale);

// Text of `key` in `table` (FUN_007556e0): the stored value, or the key itself
// when the table has no such key. A missing key appends the message the
// original writes to its log, without the final newline, to `warnings` when it
// is not null.
std::string key_text(const StringTable& table, const std::string& table_name,
                     const std::string& key, std::vector<std::string>* warnings);

// Formats the only format code of the game data (FUN_00755970): `%d` becomes
// the signed decimal of `value` and `%%` a percent sign. Any other `%` throws
// common::ParseError, so no text of the data reaches printf as a format. The
// original formats into a 256-byte buffer and its longest formatted text is
// 137 bytes, so nothing is truncated here.
std::string format_count(const std::string& text, std::int32_t value);

}  // namespace bo2::engine
