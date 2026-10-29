#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

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

bool has_non_ascii(const std::string& text) {
    for (const char character : text) {
        if (static_cast<unsigned char>(character) >= 0x80) {
            return true;
        }
    }
    return false;
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
                // The stored text is ASCII; the displayed text carries the accent.
                CHECK(!has_non_ascii(*credits));
                const std::string shown =
                    bo2::engine::display_text(*credits, bo2::engine::kDefaultLocale);
                CHECK(has_non_ascii(shown));
                CHECK(shown.size() > credits->size());
            }
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
