#include "bo2/engine/entity_template.hpp"

#include <cctype>
#include <set>
#include <sstream>

namespace bo2::engine {

namespace {

constexpr const char* kPrefix = "assets/etypes/";
constexpr const char* kExtension = ".etp";
constexpr int kMaxIncludeDepth = 16;

std::string lower(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

std::string normalize_path(const std::string& name) {
    std::string result = lower(name);
    for (char& character : result) {
        if (character == '\\') {
            character = '/';
        }
    }
    return result;
}

void parse_into(const std::string& text, const EntityTemplateSource* source, int depth,
                std::set<std::string>& active, EntityTemplate& out) {
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        const std::size_t comment = line.find("//");
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        std::istringstream tokens(line);
        std::string keyword;
        std::string argument;
        if (!(tokens >> keyword)) {
            continue;
        }
        tokens >> argument;
        keyword = lower(keyword);
        if (keyword == "include") {
            const std::string path = normalize_path(argument);
            if (source == nullptr || depth >= kMaxIncludeDepth || active.count(path) != 0) {
                continue;
            }
            const std::string* included = source->find(path);
            if (included == nullptr) {
                continue;
            }
            active.insert(path);
            parse_into(*included, source, depth + 1, active, out);
            active.erase(path);
        } else if (keyword == "model") {
            out.models.push_back(argument);
        } else if (keyword == "flag") {
            if (lower(argument) == "helperobj") {
                out.helper_object = true;
            }
        }
    }
}

bool is_template_name(const std::string& normalized) {
    const std::string prefix = kPrefix;
    const std::string extension = kExtension;
    return normalized.size() > prefix.size() + extension.size() &&
           normalized.compare(0, prefix.size(), prefix) == 0 &&
           normalized.compare(normalized.size() - extension.size(), extension.size(),
                              extension) == 0;
}

}  // namespace

EntityTemplateLibrary::EntityTemplateLibrary(const BigArchive& archive) {
    for (const BigEntry& entry : archive.entries()) {
        if (!iequals(entry.type, "strmfile") || !is_template_name(normalize_path(entry.name))) {
            continue;
        }
        const std::vector<std::uint8_t> bytes = archive.extract(entry);
        add(entry.name, std::string(bytes.begin(), bytes.end()));
    }
}

void EntityTemplateLibrary::add(const std::string& name, std::string text) {
    std::string path = normalize_path(name);
    if (!is_template_name(path)) {
        return;
    }
    const std::size_t prefix_size = std::string(kPrefix).size();
    path = path.substr(prefix_size, path.size() - prefix_size - std::string(kExtension).size());
    const std::size_t slash = path.rfind('/');
    const std::string type = slash == std::string::npos ? path : path.substr(slash + 1);
    by_type_.emplace(type, path);
    texts_[path] = std::move(text);
}

const std::string* EntityTemplateLibrary::find(const std::string& path) const {
    const auto found = texts_.find(path);
    return found == texts_.end() ? nullptr : &found->second;
}

bool EntityTemplateLibrary::find_type(const std::string& type_name, EntityTemplate& out) const {
    const auto found = by_type_.find(lower(type_name));
    if (found == by_type_.end()) {
        return false;
    }
    out = parse_entity_template(texts_.at(found->second), this);
    return true;
}

EntityTemplate parse_entity_template(const std::string& text, const EntityTemplateSource* source) {
    EntityTemplate result;
    std::set<std::string> active;
    parse_into(text, source, 0, active, result);
    return result;
}

bool entity_template_is_drawn(const EntityTemplate& entity_template) {
    return !entity_template.helper_object;
}

}  // namespace bo2::engine
