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
    std::vector<std::string> models;  // `model` lines, in order, lower case
    bool helper_object = false;       // `flag helperobj`
};

// The entity templates of one level container: its `strmfile` entries named
// `assets/etypes/<path>.etp`.
//
// Text format, one command per line (lines end at `\n` or `\r`):
//   - the line is lower-cased, then split on white space;
//   - a line whose first token starts with `//` is a comment;
//   - `model <name>` appends a model;
//   - `flag <name>`; only `helperobj` is kept. The original skips drawing an
//     entity whose template carries it (`FUN_0070d0d5` copies the flag to
//     entity bit 0x200, `FUN_0070f100` returns without drawing);
//   - `include <path>` splices the included template in place, so its
//     models and flags come before the lines that follow.
class EntityTemplateLibrary {
public:
    EntityTemplateLibrary() = default;
    explicit EntityTemplateLibrary(const BigArchive& archive);

    // `name` is a `strmfile` entry name; entries outside `assets/etypes/`
    // or without the `.etp` extension are ignored.
    void add(const std::string& name, std::string text);

    // Resolves a region entity type, or an `include` operand, to a template
    // and parses it. `name` is a path relative to `assets/etypes/`, with or
    // without `.etp` (`camera/animcamera`, `animated/animated_nocull`). The
    // exact path is tried first, then the file name alone (the first
    // template of that name, so `include camera` finds `camera/camera.etp`).
    // Returns false when no template matches.
    bool find_type(const std::string& name, EntityTemplate& out) const;

private:
    const std::string* resolve(const std::string& name, std::string& key) const;
    const EntityTemplate& parsed(const std::string& key) const;
    EntityTemplate parse(const std::string& key) const;

    std::map<std::string, std::string> texts_;      // path without extension -> text
    std::map<std::string, std::string> by_base_;    // file name -> path
    // Parsed templates, so a template included by many others is parsed once
    // (an include graph fans out exponentially otherwise).
    mutable std::map<std::string, EntityTemplate> cache_;
    mutable std::vector<std::string> active_;       // include chain, for cycles
};

}  // namespace bo2::engine
