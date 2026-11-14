#include "bo2/engine/string_table.hpp"

#include "bo2/common/reader.hpp"

namespace bo2::engine {

namespace {

constexpr std::size_t kHeaderSize = 20;
constexpr std::uint16_t kRecordTag = 7;
constexpr std::uint16_t kTerminator = 0xFFFF;

const std::string& name_of(const BigArchive& archive, const BigEntry& entry, std::uint16_t ref) {
    if (ref >= entry.symbols.size()) {
        throw common::ParseError("strtable reference is outside the symbol list");
    }
    const std::uint16_t symbol = entry.symbols[ref];
    if (symbol >= archive.names().size()) {
        throw common::ParseError("strtable symbol is outside the name table");
    }
    return archive.names()[symbol];
}

}  // namespace

StringTable StringTable::parse(const BigArchive& archive, const BigEntry& entry) {
    const std::vector<std::uint8_t> payload = archive.extract(entry);
    const common::Reader reader(payload);
    if (payload.size() < kHeaderSize || reader.cstr(0, 8) != "default") {
        throw common::ParseError("not a strtable payload");
    }
    // The header holds the payload size, then the constants 1 and 2.
    if (reader.u32(8) != payload.size()) {
        throw common::ParseError("strtable size does not match the payload");
    }
    if (((payload.size() - kHeaderSize) & 1U) != 0U) {
        throw common::ParseError("strtable payload is truncated");
    }

    StringTable table;
    std::vector<std::uint16_t> record;
    for (std::size_t offset = kHeaderSize; offset < payload.size(); offset += 2) {
        const std::uint16_t word = reader.u16(offset);
        if (word != kTerminator) {
            record.push_back(word);
            continue;
        }
        if (record.size() == 3 && record[0] == kRecordTag) {
            table.records_.push_back(
                {name_of(archive, entry, record[1]), name_of(archive, entry, record[2])});
        } else {
            table.irregular_.push_back(record);
        }
        record.clear();
    }
    if (!record.empty()) {
        throw common::ParseError("strtable record has no terminator");
    }
    return table;
}

const std::string* StringTable::find(const std::string& key) const {
    for (const StringTableRecord& record : records_) {
        if (iequals(record.key, key)) {
            return &record.value;
        }
    }
    return nullptr;
}

std::string display_text(const std::string& stored, const std::string&) {
    std::string text;
    text.reserve(stored.size() * 2);
    for (const char character : stored) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte >= 0x80) {
            // A raw Latin-1 byte in the stored name (one Spanish and one German record).
            text.push_back(static_cast<char>(0xC0 | (byte >> 6)));
            text.push_back(static_cast<char>(0x80 | (byte & 0x3F)));
            continue;
        }
        switch (character) {
            case 'A': text += "\xC3\xA7"; break;  // c-cedilla
            case 'B': text += "\xC3\xBC"; break;  // u-diaeresis
            case 'C': text += "\xC3\xA9"; break;  // e-acute
            case 'D': text += "\xC3\xA2"; break;  // a-circumflex
            case 'E': text += "\xC3\xA4"; break;  // a-diaeresis
            case 'F': text += "\xC3\xA0"; break;  // a-grave
            case 'G': text += "\xC3\x9F"; break;  // sharp s
            case 'H': text += "\xC3\xAA"; break;  // e-circumflex
            case 'I': text += "\xC3\xAB"; break;  // e-diaeresis
            case 'J': text += "\xC3\xA8"; break;  // e-grave
            case 'K': text += "\xC3\xAF"; break;  // i-diaeresis
            case 'L': text += "\xC3\xAE"; break;  // i-circumflex
            case 'M': text += "\xC3\xAC"; break;  // i-grave
            case 'N': text += "\xC3\xB4"; break;  // o-circumflex
            case 'O': text += "\xC3\xB6"; break;  // o-diaeresis
            case 'P': text += "\xC3\xB2"; break;  // o-grave
            case 'Q': text += "\xC3\xBB"; break;  // u-circumflex
            case 'R': text += "\xC3\xB9"; break;  // u-grave
            case 'S': text += "?"; break;         // question mark
            case 'T': text += "\xC3\xA1"; break;  // a-acute
            case 'U': text += "\xC3\xAD"; break;  // i-acute
            case 'V': text += "\xC3\xB3"; break;  // o-acute
            case 'W': text += "\xC3\xBA"; break;  // u-acute
            case 'X': text += "\xC3\xB1"; break;  // n-tilde
            case 'Y': text += "\xC2\xBF"; break;  // inverted question mark
            case 'Z': text += "\xC2\xA1"; break;  // inverted exclamation mark
            default: text.push_back(character); break;
        }
    }
    return text;
}

}  // namespace bo2::engine
