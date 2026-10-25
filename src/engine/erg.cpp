#include "bo2/engine/erg.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace bo2::engine {

namespace {

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

std::string quoted_text(const std::string& text, std::size_t& position, int line) {
    ++position;
    const std::size_t close = text.find('"', position);
    if (close == std::string::npos) {
        throw std::runtime_error("unterminated quote on line " + std::to_string(line));
    }
    std::string result = text.substr(position, close - position);
    position = close + 1;
    return result;
}

}  // namespace

ErgConfig ErgConfig::parse(const std::string& text) {
    ErgConfig config;
    std::size_t position = 0;
    int line = 1;
    while (position < text.size()) {
        std::size_t end = text.find('\n', position);
        const bool has_newline = end != std::string::npos;
        if (!has_newline) {
            end = text.size();
        }
        std::string line_text = text.substr(position, end - position);
        position = has_newline ? end + 1 : text.size();
        if (!line_text.empty() && line_text.back() == '\r') {
            line_text.pop_back();
        }
        ++line;
        const std::string trimmed = trim(line_text);
        if (trimmed.empty()) {
            ErgEntry blank;
            blank.blank = true;
            config.entries_.push_back(blank);
            continue;
        }
        ErgEntry entry;
        std::size_t cursor = 0;
        if (trimmed[cursor] == '"') {
            entry.key_quoted = true;
            entry.key = quoted_text(trimmed, cursor, line - 1);
        } else {
            const std::size_t equals = trimmed.find('=', cursor);
            if (equals == std::string::npos) {
                throw std::runtime_error("missing '=' on line " + std::to_string(line - 1));
            }
            entry.key = trim(trimmed.substr(cursor, equals - cursor));
            cursor = equals;
        }
        while (cursor < trimmed.size() && (trimmed[cursor] == ' ' || trimmed[cursor] == '\t')) {
            ++cursor;
        }
        if (cursor >= trimmed.size() || trimmed[cursor] != '=') {
            throw std::runtime_error("missing '=' on line " + std::to_string(line - 1));
        }
        ++cursor;
        while (cursor < trimmed.size() && (trimmed[cursor] == ' ' || trimmed[cursor] == '\t')) {
            ++cursor;
        }
        if (cursor < trimmed.size() && trimmed[cursor] == '"') {
            entry.value_quoted = true;
            entry.value = quoted_text(trimmed, cursor, line - 1);
            while (cursor < trimmed.size() &&
                   (trimmed[cursor] == ' ' || trimmed[cursor] == '\t')) {
                ++cursor;
            }
            if (cursor != trimmed.size()) {
                throw std::runtime_error("trailing text on line " + std::to_string(line - 1));
            }
        } else {
            entry.value = trim(trimmed.substr(cursor));
        }
        config.entries_.push_back(entry);
    }
    return config;
}

std::string ErgConfig::serialize(bool crlf) const {
    const std::string newline = crlf ? "\r\n" : "\n";
    std::string result;
    for (const ErgEntry& entry : entries_) {
        if (entry.blank) {
            result += newline;
            continue;
        }
        if (entry.key_quoted || entry.key.empty()) {
            result += '"';
            result += entry.key;
            result += '"';
        } else {
            result += entry.key;
        }
        result += '=';
        if (entry.value_quoted || entry.value.empty()) {
            result += '"';
            result += entry.value;
            result += '"';
        } else {
            result += entry.value;
        }
        result += newline;
    }
    return result;
}

const std::string* ErgConfig::find(const std::string& key) const {
    for (const ErgEntry& entry : entries_) {
        if (!entry.blank && entry.key == key) {
            return &entry.value;
        }
    }
    return nullptr;
}

}  // namespace bo2::engine
