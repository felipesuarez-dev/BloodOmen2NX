#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "bo2/engine/big_archive.hpp"

namespace bo2::engine {

// Runs the reader of one entry type over one entry. It throws std::exception
// when the reader rejects the entry (docs/DATA_COVERAGE.md).
using EntryCheck = void (*)(const BigArchive&, const BigEntry&);

struct DataFailure {
    std::string container;
    std::string type;
    std::string entry;
    std::string message;
};

struct TypeCount {
    std::size_t entries = 0;
    std::size_t checked = 0;
    std::size_t failed = 0;
};

struct DataReport {
    std::size_t containers = 0;
    std::map<std::string, TypeCount> types;
    std::vector<DataFailure> failures;
};

// The readers the data checker knows, by entry type.
const std::map<std::string, EntryCheck>& entry_checks();

// Counts every entry of the archive by type and runs the registered reader of
// each entry. A reader that throws counts as one failure and does not stop
// the walk.
void check_archive(const BigArchive& archive, const std::string& container, DataReport& report);

}  // namespace bo2::engine
