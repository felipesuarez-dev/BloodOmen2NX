#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/entity_template.hpp"

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
using bo2::engine::entity_template_is_drawn;
using bo2::engine::parse_entity_template;

void test_flag_and_model() {
    const EntityTemplate helper = parse_entity_template(
        "model\t\tsfxobj\\sfxobj\n//Invisible object used for placement\n"
        "goscript\tAmbSoundScriptV2\nflag\t\thelperobj\nflag\t\tstatic\n",
        nullptr);
    CHECK(helper.helper_object);
    CHECK(helper.models.size() == 1);
    CHECK(helper.models[0] == "sfxobj\\sfxobj");
    CHECK(!entity_template_is_drawn(helper));

    const EntityTemplate prop = parse_entity_template("model crate\nflag static\n", nullptr);
    CHECK(!prop.helper_object);
    CHECK(entity_template_is_drawn(prop));
}

void test_case_and_comments() {
    // Flag names are lower-cased before the comparison (FUN_0068f430).
    CHECK(parse_entity_template("flag HelperObj\n", nullptr).helper_object);
    // A trailing `//` comment never reaches the parser.
    CHECK(!parse_entity_template("model pyramid // flag helperobj\n", nullptr).helper_object);
    CHECK(!parse_entity_template("// flag helperobj\n", nullptr).helper_object);
    // `helperobj` as a model argument is not a flag.
    CHECK(!parse_entity_template("model helperobj\n", nullptr).helper_object);
    // CRLF line endings.
    CHECK(parse_entity_template("model a\r\nflag helperobj\r\n", nullptr).helper_object);
}

void test_include() {
    EntityTemplateLibrary library;
    library.add("assets/etypes/camera/ExportedCamera.etp",
                "model camera/ExportedCamera\nflag notPlaceable\nflag helperobj\n");
    library.add("assets/etypes/camera/camera.etp", "include camera/ExportedCamera\n");
    library.add("assets/etypes/props/barrel.etp", "include props/missing\nmodel barrel\n");
    library.add("assets/etypes/loop/a.etp", "include loop/b\nmodel a\n");
    library.add("assets/etypes/loop/b.etp", "include loop/a\nflag helperobj\n");
    library.add("strings/not_a_template.etp", "flag helperobj\n");
    library.add("assets/etypes/props/ignored.txt", "flag helperobj\n");

    EntityTemplate found;
    // An include splices the included flags in (camera.etp has no flag line).
    CHECK(library.find_type("camera", found));
    CHECK(found.helper_object);
    CHECK(found.models.size() == 1);
    CHECK(found.models[0] == "camera/ExportedCamera");

    // Type lookup ignores case; an unknown include is skipped.
    CHECK(library.find_type("BARREL", found));
    CHECK(!found.helper_object);
    CHECK(found.models.size() == 1);

    // Include cycles terminate.
    CHECK(library.find_type("a", found));
    CHECK(found.helper_object);

    // Files outside assets/etypes or without .etp are not templates.
    CHECK(!library.find_type("not_a_template", found));
    CHECK(!library.find_type("ignored", found));
    CHECK(!library.find_type("nothing", found));
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
    for (const char* type : {"animated", "psntanm", "umahvf", "rat"}) {
        CHECK(library.find_type(type, found));
        CHECK(!found.helper_object);
    }
}

}  // namespace

int main() {
    try {
        test_flag_and_model();
        test_case_and_comments();
        test_include();
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
