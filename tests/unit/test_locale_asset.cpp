#include <cstdint>
#include <cstdlib>
#include <filesystem>
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

}  // namespace

int main() {
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
