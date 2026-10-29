#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/entity_template.hpp"
#include "bo2/engine/region.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

using bo2::engine::EntityTemplate;
using bo2::engine::EntityTemplateLibrary;

// The template of a one-file library; `text` is the file content.
EntityTemplate parse(const std::string& text) {
    EntityTemplateLibrary library;
    library.add("assets/etypes/t.etp", text);
    EntityTemplate result;
    CHECK(library.find_type("t", result));
    return result;
}

void test_flag_and_model() {
    const EntityTemplate helper = parse(
        "model\t\tsfxobj\\sfxobj\n//Invisible object used for placement\n"
        "goscript\tAmbSoundScriptV2\nflag\t\thelperobj\nflag\t\tstatic\n");
    CHECK(helper.helper_object);
    CHECK(helper.models.size() == 1);
    CHECK(helper.models[0] == "sfxobj\\sfxobj");

    const EntityTemplate prop = parse("model crate\nflag static\n");
    CHECK(!prop.helper_object);
    CHECK(prop.models.size() == 1);
}

void test_tokenizer() {
    // The whole line is lower-cased (FUN_0068f430).
    CHECK(parse("flag HelperObj\n").helper_object);
    CHECK(parse("FLAG HELPEROBJ\n").helper_object);
    CHECK(parse("Model Camera/ExportedCamera\n").models[0] == "camera/exportedcamera");
    // A line whose first token starts with `//` is a comment.
    CHECK(!parse("// flag helperobj\n").helper_object);
    CHECK(!parse("//flag helperobj\n").helper_object);
    // A trailing comment is extra tokens after the operand.
    CHECK(!parse("model pyramid // flag helperobj\n").helper_object);
    CHECK(parse("flag helperobj // placement only\n").helper_object);
    // `helperobj` as a model operand is not a flag.
    CHECK(!parse("model helperobj\n").helper_object);
    // Lines end at \n, \r or \r\n.
    CHECK(parse("model a\r\nflag helperobj\r\n").helper_object);
    CHECK(parse("model a\rflag helperobj\r").helper_object);
    CHECK(parse("model a\rflag helperobj\r").models.size() == 1);
    // A keyword without an operand is ignored.
    CHECK(!parse("flag\nmodel\n").helper_object);
}

void test_include_order() {
    EntityTemplateLibrary library;
    library.add("assets/etypes/base.etp", "model a\ninclude inc\nmodel c\n");
    library.add("assets/etypes/inc.etp", "model b\nflag helperobj\n");
    EntityTemplate found;
    // The included lines are spliced where the include stands.
    CHECK(library.find_type("base", found));
    CHECK(found.models.size() == 3);
    CHECK(found.models == (std::vector<std::string>{"a", "b", "c"}));
    CHECK(found.helper_object);
    // The included template alone is unchanged by being included.
    CHECK(library.find_type("inc", found));
    CHECK(found.models == (std::vector<std::string>{"b"}));
}

void test_type_and_include_resolution() {
    EntityTemplateLibrary library;
    library.add("assets/etypes/camera/ExportedCamera.etp",
                "model camera/ExportedCamera\nflag notPlaceable\nflag helperobj\n");
    // `.etp` on the operand is stripped (608 includes carry it).
    library.add("assets/etypes/camera/camera.etp", "include camera/ExportedCamera.etp\n");
    // A bare name resolves to the template of that file name.
    library.add("assets/etypes/camera/animcamera.etp", "include camera\nmodel animcamera\n");
    library.add("assets/etypes/animated/animated_nocull.etp", "model pyramid\nflag nevercull\n");
    library.add("assets/etypes/props/barrel.etp", "include props/missing\nmodel barrel\n");
    // Two templates with one file name: the path decides, in either order.
    library.add("assets/etypes/levels/a/arrives/arrives.etp", "flag helperobj\n");
    library.add("assets/etypes/levels/b/arrives/arrives.etp", "model arrives\n");
    library.add("strings/not_a_template.etp", "flag helperobj\n");
    library.add("assets/etypes/props/ignored.txt", "flag helperobj\n");

    EntityTemplate found;
    CHECK(library.find_type("camera", found));
    CHECK(found.helper_object);
    CHECK(found.models == (std::vector<std::string>{"camera/exportedcamera"}));

    // Region entity types can be paths.
    CHECK(library.find_type("camera/animcamera", found));
    CHECK(found.helper_object);
    CHECK(found.models == (std::vector<std::string>{"camera/exportedcamera", "animcamera"}));
    CHECK(library.find_type("animated/animated_nocull", found));
    CHECK(!found.helper_object);
    CHECK(library.find_type("Animated\\Animated_NoCull", found));
    CHECK(library.find_type("levels/a/arrives/arrives", found));
    CHECK(found.helper_object);
    CHECK(library.find_type("levels/b/arrives/arrives", found));
    CHECK(!found.helper_object);
    // The file name alone finds the first template of that name.
    CHECK(library.find_type("arrives", found));
    CHECK(found.helper_object);
    CHECK(library.find_type("animated_nocull", found));

    // An unknown include is skipped.
    CHECK(library.find_type("BARREL", found));
    CHECK(!found.helper_object);
    CHECK(found.models == (std::vector<std::string>{"barrel"}));

    // Files outside assets/etypes or without .etp are not templates.
    CHECK(!library.find_type("not_a_template", found));
    CHECK(!library.find_type("ignored", found));
    CHECK(!library.find_type("nothing", found));
}

void test_include_cycle() {
    EntityTemplateLibrary library;
    library.add("assets/etypes/loop/a.etp", "include loop/b\nmodel a\n");
    library.add("assets/etypes/loop/b.etp", "include loop/a\nflag helperobj\n");
    EntityTemplate found;
    CHECK(library.find_type("loop/a", found));
    CHECK(found.helper_object);
}

void test_include_fan_out_is_not_exponential() {
    // Every level includes the next one three times: 3^15 expansions if the
    // included templates were parsed again at each include.
    EntityTemplateLibrary library;
    constexpr int depth = 16;
    for (int level = 0; level < depth; ++level) {
        const std::string next = "include fan/l" + std::to_string(level + 1) + "\n";
        std::string text = "model m" + std::to_string(level) + "\n";
        if (level + 1 < depth) {
            text += next + next + next;
        } else {
            text += "flag helperobj\n";
        }
        library.add("assets/etypes/fan/l" + std::to_string(level) + ".etp", text);
    }
    const auto start = std::chrono::steady_clock::now();
    EntityTemplate found;
    CHECK(library.find_type("fan/l0", found));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(found.helper_object);
    CHECK(!found.models.empty());
    CHECK(found.models.size() <= 256);
    CHECK(elapsed < std::chrono::seconds(2));
}

void test_sl01_templates() {
    const char* root = std::getenv("BO2NX_ASSETS_DIR");
    if (root == nullptr) {
        std::cout << "entity template asset check skipped (BO2NX_ASSETS_DIR is unset)\n";
        return;
    }
    const std::filesystem::path path =
        std::filesystem::path(root) / "media/gogames/bo2/resources/levels/slums/sl01.big";
    if (!std::filesystem::exists(path)) {
        std::cout << "entity template asset check skipped (sl01.big not found)\n";
        return;
    }
    const bo2::engine::BigArchive archive = bo2::engine::BigArchive::load(path.string());
    const EntityTemplateLibrary library(archive);
    EntityTemplate found;
    // Helper objects: the spawn emitter, SFX anchors, region and level
    // keepers, and cameras (camera.etp includes ExportedCamera.etp).
    for (const char* type : {"kainemitter", "sfxobj", "regionkeeper", "levelkeeper", "camera",
                             "hidden_checkpoint", "hint_proximity"}) {
        CHECK(library.find_type(type, found));
        CHECK(found.helper_object);
    }
    // Entities that are drawn through other rules.
    for (const char* type : {"animated", "psntanm", "umahvf", "rat", "detector"}) {
        CHECK(library.find_type(type, found));
        CHECK(!found.helper_object);
    }

    // The region table of the level: 47 of its 104 entities are helpers.
    std::size_t entities = 0;
    std::size_t helpers = 0;
    std::size_t unresolved = 0;
    for (const bo2::engine::BigEntry& entry : archive.entries()) {
        if (!bo2::engine::iequals(entry.type, "region")) {
            continue;
        }
        for (const bo2::engine::RegionEntityTable& table :
             bo2::engine::parse_region_entities(archive.extract(entry))) {
            for (const bo2::engine::RegionEntity& entity : table.entities) {
                ++entities;
                if (!library.find_type(entity.model, found)) {
                    ++unresolved;
                } else if (found.helper_object) {
                    ++helpers;
                }
            }
        }
    }
    CHECK(entities == 104);
    CHECK(helpers == 47);
    CHECK(unresolved == 0);
}

}  // namespace

int main() {
    try {
        test_flag_and_model();
        test_tokenizer();
        test_include_order();
        test_type_and_include_resolution();
        test_include_cycle();
        test_include_fan_out_is_not_exponential();
        test_sl01_templates();
    } catch (const std::exception& error) {
        std::cerr << "entity template tests: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "entity template tests: ok\n";
    return 0;
}
