#include "bo2/engine/entity_template.hpp"

#include <algorithm>
#include <cctype>

namespace bo2::engine {

namespace {

constexpr const char* kPrefix = "assets/etypes/";
constexpr const char* kExtension = ".etp";
// A template names a handful of models; the cap only bounds the output of
// pathological include graphs, where an include repeated on many lines
// would otherwise multiply the list.
constexpr std::size_t kMaxModels = 256;

std::string normalize(const std::string& name) {
    std::string result = name;
    for (char& character : result) {
        character = character == '\\'
                        ? '/'
                        : static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return result;
}

bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string strip_extension(std::string path) {
    const std::string extension = kExtension;
    if (ends_with(path, extension)) {
        path.erase(path.size() - extension.size());
    }
    return path;
}

std::string base_name(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool is_template_name(const std::string& normalized) {
    const std::string prefix = kPrefix;
    return normalized.size() > prefix.size() + std::string(kExtension).size() &&
           normalized.compare(0, prefix.size(), prefix) == 0 && ends_with(normalized, kExtension);
}

// Splits at `\n` or `\r`, as `FUN_00785c8d` does.
std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::string line;
    for (char character : text) {
        if (character == '\n' || character == '\r') {
            lines.push_back(std::move(line));
            line.clear();
        } else {
            line.push_back(character);
        }
    }
    lines.push_back(std::move(line));
    return lines;
}

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string token;
    for (char character : line) {
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            if (!token.empty()) {
                tokens.push_back(std::move(token));
                token.clear();
            }
        } else {
            token.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
    }
    if (!token.empty()) {
        tokens.push_back(std::move(token));
    }
    return tokens;
}

}  // namespace

EntityTemplateLibrary::EntityTemplateLibrary(const BigArchive& archive) {
    for (const BigEntry& entry : archive.entries()) {
        if (!iequals(entry.type, "strmfile") || !is_template_name(normalize(entry.name))) {
            continue;
        }
        const std::vector<std::uint8_t> bytes = archive.extract(entry);
        add(entry.name, std::string(bytes.begin(), bytes.end()));
    }
}

void EntityTemplateLibrary::add(const std::string& name, std::string text) {
    const std::string normalized = normalize(name);
    if (!is_template_name(normalized)) {
        return;
    }
    const std::size_t prefix_size = std::string(kPrefix).size();
    const std::string path = strip_extension(normalized.substr(prefix_size));
    by_base_.emplace(base_name(path), path);
    texts_[path] = std::move(text);
}

const std::string* EntityTemplateLibrary::resolve(const std::string& name, std::string& key) const {
    const std::string path = strip_extension(normalize(name));
    auto found = texts_.find(path);
    if (found == texts_.end()) {
        const auto base = by_base_.find(base_name(path));
        if (base == by_base_.end()) {
            return nullptr;
        }
        found = texts_.find(base->second);
    }
    key = found->first;
    return &found->second;
}

const EntityTemplate& EntityTemplateLibrary::parsed(const std::string& key) const {
    static const EntityTemplate empty;
    const auto cached = cache_.find(key);
    if (cached != cache_.end()) {
        return cached->second;
    }
    // Cyclic includes are invalid data; the walk stops at the repeat, so the
    // result for a template inside a cycle depends on where the walk began.
    if (std::find(active_.begin(), active_.end(), key) != active_.end()) {
        return empty;
    }
    active_.push_back(key);
    EntityTemplate result = parse(key);
    active_.pop_back();
    return cache_.emplace(key, std::move(result)).first->second;
}

EntityTemplate EntityTemplateLibrary::parse(const std::string& key) const {
    EntityTemplate result;
    for (const std::string& line : split_lines(texts_.at(key))) {
        const std::vector<std::string> tokens = tokenize(line);
        if (tokens.size() < 2 || tokens[0].compare(0, 2, "//") == 0) {
            continue;
        }
        const std::string& keyword = tokens[0];
        const std::string& argument = tokens[1];
        if (keyword == "include") {
            std::string included_key;
            if (resolve(argument, included_key) == nullptr) {
                continue;
            }
            const EntityTemplate& included = parsed(included_key);
            for (const std::string& model : included.models) {
                if (result.models.size() < kMaxModels) {
                    result.models.push_back(model);
                }
            }
            result.helper_object = result.helper_object || included.helper_object;
        } else if (keyword == "model") {
            if (result.models.size() < kMaxModels) {
                result.models.push_back(argument);
            }
        } else if (keyword == "flag" && argument == "helperobj") {
            result.helper_object = true;
        }
    }
    return result;
}

bool EntityTemplateLibrary::find_type(const std::string& name, EntityTemplate& out) const {
    std::string key;
    if (resolve(name, key) == nullptr) {
        return false;
    }
    out = parsed(key);
    return true;
}

}  // namespace bo2::engine
