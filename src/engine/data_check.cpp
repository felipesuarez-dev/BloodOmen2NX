#include "bo2/engine/data_check.hpp"

#include <exception>

#include "bo2/engine/anim_graph.hpp"
#include "bo2/engine/anim_reel.hpp"
#include "bo2/engine/cell_group.hpp"
#include "bo2/engine/dds.hpp"
#include "bo2/engine/pixmap_font.hpp"
#include "bo2/engine/region.hpp"
#include "bo2/engine/string_table.hpp"

namespace bo2::engine {

namespace {

void check_animatio(const BigArchive& archive, const BigEntry& entry) {
    static_cast<void>(parse_anim_graph(archive.extract(entry)));
}

void check_animreel(const BigArchive& archive, const BigEntry& entry) {
    static_cast<void>(parse_anim_reel(archive.extract(entry)));
}

void check_cellgrup(const BigArchive& archive, const BigEntry& entry) {
    static_cast<void>(parse_cell_group(archive.extract(entry), entry.symbols, archive.names()));
}

void check_model(const BigArchive& archive, const BigEntry& entry) {
    static_cast<void>(parse_model_asset(archive.extract(entry), entry.symbols, archive.names()));
}

void check_texfont(const BigArchive& archive, const BigEntry& entry) {
    static_cast<void>(parse_pixmap_font(archive.extract(entry), entry.symbols, archive.names()));
}

void check_region(const BigArchive& archive, const BigEntry& entry) {
    static_cast<void>(parse_region_entities(archive.extract(entry)));
}

void check_strtable(const BigArchive& archive, const BigEntry& entry) {
    static_cast<void>(StringTable::parse(archive, entry));
}

void check_texture(const BigArchive& archive, const BigEntry& entry) {
    static_cast<void>(decode_dds(archive.extract(entry)));
}

}  // namespace

const std::map<std::string, EntryCheck>& entry_checks() {
    static const std::map<std::string, EntryCheck> checks = {
        {"animatio", &check_animatio},
        {"animreel", &check_animreel},
        {"cellgrup", &check_cellgrup},
        {"model", &check_model},
        {"region", &check_region},
        {"strtable", &check_strtable},
        {"texfont", &check_texfont},
        {"texture", &check_texture},
    };
    return checks;
}

void check_archive(const BigArchive& archive, const std::string& container, DataReport& report) {
    ++report.containers;
    const std::map<std::string, EntryCheck>& checks = entry_checks();
    for (const BigEntry& entry : archive.entries()) {
        TypeCount& count = report.types[entry.type];
        ++count.entries;
        const auto check = checks.find(entry.type);
        if (check == checks.end()) {
            continue;
        }
        ++count.checked;
        try {
            check->second(archive, entry);
        } catch (const std::exception& error) {
            ++count.failed;
            report.failures.push_back(
                DataFailure{container, entry.type, entry.name, error.what()});
        }
    }
}

}  // namespace bo2::engine
