#pragma once

#include <map>
#include <string>
#include <vector>

#include "bo2/engine/big_archive.hpp"

namespace bo2::engine {

// An entity template ("ETP") is the text file that describes a region
// entity type: its models, behavior, script and flags (`FUN_00785c8d`).
// Only the fields the host needs are kept.
struct EntityTemplate {
    std::vector<std::string> models;  // `model` lines, in order
    bool helper_object = false;       // `flag helperobj`
};

// Lookup for `include` lines: the template path relative to
// `assets/etypes/`, without extension, lower case, `/` separators. Returns
// the template text, or nullptr when the path is unknown.
class EntityTemplateSource {
public:
    virtual ~EntityTemplateSource() = default;
    virtual const std::string* find(const std::string& path) const = 0;
};

// Templates keyed by their `assets/etypes/` path, as stored in a level
// container (`strmfile` entries ending in `.etp`).
class EntityTemplateLibrary : public EntityTemplateSource {
public:
    EntityTemplateLibrary() = default;
    explicit EntityTemplateLibrary(const BigArchive& archive);

    // `name` is a `strmfile` entry name; entries outside `assets/etypes/`
    // or without the `.etp` extension are ignored.
    void add(const std::string& name, std::string text);
    const std::string* find(const std::string& path) const override;

    // Resolves an entity type name (the model field of a region entity) to
    // its template. The type is the file name without directory or
    // extension; returns false when the level has no such template.
    bool find_type(const std::string& type_name, EntityTemplate& out) const;

private:
    std::map<std::string, std::string> texts_;
    std::map<std::string, std::string> by_type_;
};

// Parses template text. `include <path>` splices the included template in
// place, so its lines (and flags) are processed before the lines that
// follow. `//` starts a comment; keywords and flag names are lower-cased.
EntityTemplate parse_entity_template(const std::string& text, const EntityTemplateSource* source);

// The original skips drawing an entity whose template carries
// `flag helperobj` (`FUN_0070d0d5` copies the flag to entity bit 0x200,
// `FUN_0070f100` returns without drawing when the bit is set).
bool entity_template_is_drawn(const EntityTemplate& entity_template);

}  // namespace bo2::engine
