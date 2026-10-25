#pragma once

#include <string>
#include <vector>

namespace bo2::engine {

struct ErgEntry {
    std::string key;
    std::string value;
    bool key_quoted = false;
    bool value_quoted = false;
    bool blank = false;
};

class ErgConfig {
public:
    static ErgConfig parse(const std::string& text);

    std::string serialize(bool crlf = true) const;
    const std::string* find(const std::string& key) const;
    const std::vector<ErgEntry>& entries() const { return entries_; }

private:
    std::vector<ErgEntry> entries_;
};

}  // namespace bo2::engine
