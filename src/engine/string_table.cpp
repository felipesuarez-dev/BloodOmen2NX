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

std::string display_text(const std::string& stored, const std::string& locale) {
    if (!iequals(locale, "spanish")) {
        return stored;
    }
    std::string text;
    text.reserve(stored.size());
    for (const char character : stored) {
        switch (character) {
            case 'T': text += "\xC3\xA1"; break;  // a-acute
            case 'C': text += "\xC3\xA9"; break;  // e-acute
            case 'U': text += "\xC3\xAD"; break;  // i-acute
            case 'V': text += "\xC3\xB3"; break;  // o-acute
            case 'W': text += "\xC3\xBA"; break;  // u-acute
            case 'X': text += "\xC3\xB1"; break;  // n-tilde
            case 'Y': text += "\xC2\xBF"; break;  // inverted question mark
            default: text.push_back(character); break;
        }
    }
    return text;
}

}  // namespace bo2::engine
