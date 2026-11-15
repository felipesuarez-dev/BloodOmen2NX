#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "bo2/engine/big_archive.hpp"
#include "bo2/engine/script_package.hpp"
#include "bo2/engine/script_program.hpp"

#include "bo2/engine/crt_random.hpp"
#include "bo2/engine/region.hpp"
#include "bo2/engine/script_machine.hpp"

namespace bo2::engine {

// The creatures of a level and their script objects (docs/SCRIPTS.md, "The
// script objects of a level", "The update of the objects of a level"): one
// creature per entity of a region, the script object of each scripted one,
// their activation in build order and their update once per frame. It is
// the creature host of the machine.
//
// The simulation scheduler (FUN_0073e840, `schedule`) gives each creature
// its time for the frame. The caller gives what the port does not compute
// yet: the bounding radius of each creature's model, whether it is in the
// visual list, the frame it was last drawn in, the positions of the
// viewers and the tune of the level keeper; it also adds the creatures that
// are not entities (Kain, the cameras, the HUD).
class ScriptLevel final : public ScriptCreatureHost {
public:
    struct Creature {
        std::string name;                     // +0xb0
        std::string package;                  // +0x144, empty for none
        std::string script;                   // the class of its object (reserved.script)
        std::array<float, 3> position{};      // +0xb4
        ScriptObjectId object = 0;            // +0x130; 0 when +0x138 is clear
        std::uint32_t flags = 0;              // +0xf0
        std::uint32_t drawn = 0;              // +0x140
        float time = 0.0f;                    // +0xfc, the time of the next update
        float saved = 0.0f;                   // +0x100, time not given yet
        float near2 = 0.0f;                   // +0x104, squared
        float far2 = 0.0f;                    // +0x108, squared
        float radius = 0.0f;                  // +0x10c
        float wake = 0.0f;                    // +0x110, the timer of a far creature
        float distance = 0.0f;                // +0x114, squared, to the nearest viewer
        float model_radius = 0.0f;            // its model's bounding radius (virtual +0x3c), 0 for none
        bool visual = false;                  // in the visual list (+0x58, DAT_00a83da8)
        bool kain = false;                    // made by the factory of "kain" (FUN_00428640)
        bool activated = false;               // FUN_0070e010
        bool hibernating = false;             // +0x160
        bool in_process = true;               // in the group process_msg
        bool in_second = true;                // in the group DAT_00a83744
        bool removed = false;                 // +0xf0 bit 0x2000: gone at the end of the frame
        bool deleted = false;                 // gone
    };

    // The tune of the level keeper the scheduler reads
    // (CommonLevelKeeperTune of levelkeepertune.h; the tunedata entry
    // "levelkeeper" of code.big holds 0, 1200, 10, 1, 14 and 10).
    struct KeeperTune {
        float cull_dist = 0.0f;            // +0x08 creatureCullDist (DAT_00a8377c)
        float periodic_dist = 1000000.0f;  // +0x10 periodicSimDist
        float periodic_rate = 60.0f;       // +0x14 periodicSimRate
        std::uint32_t screen_cull = 0;     // +0x34 screenCullData: 1 picks the second mode
        float screen_near = 20.0f;         // +0x38
        float screen_far = 10.0f;          // +0x3c
    };

    // `random` is the C library rand the whole game draws from.
    ScriptLevel(ScriptMachine& machine, CrtRandom& random);
    ~ScriptLevel() override;
    ScriptLevel(const ScriptLevel&) = delete;
    ScriptLevel& operator=(const ScriptLevel&) = delete;

    // The bounding radius of the model of an entity's creature, 0 for none
    // (docs/SCRIPTS.md, "The model radius").
    using ModelRadius = std::function<float(const RegionEntity&)>;

    // The creatures of a region, one per entity in the order of its tables,
    // with the region's package (FUN_00783900), the class of
    // `reserved.script` (FUN_007828c0) and the radius `model_radius` gives
    // (0 for all when it is empty). Every one is in the visual list (all 274
    // entity creatures of the three slums regions were, read live). Returns
    // how many it added.
    std::size_t add_region(const std::string& region_name, const std::vector<RegionEntityTable>& tables,
                           const ModelRadius& model_radius = {});
    // A creature at the end of the creature list; returns its word (index
    // + 1). A creature with a script gets its object (FUN_0070e560): the
    // class of that name in the program, when it derives from
    // basecreaturescript; else it has none.
    std::uint32_t add_creature(Creature creature);

    // The level keeper's tune (FUN_0072c300); every creature gets its
    // distances again (FUN_0072c910).
    void set_keeper(const KeeperTune& tune);
    // The squared near and far distances of a creature (FUN_0073e0c0):
    // FUN_0073e1a0 in the second mode, else FUN_0073e0f0. `size` is the
    // size the caller passes: 1 from the constructor (0x70d7ba) and from
    // FUN_0072c910 (0x72d285).
    void set_distances(std::uint32_t word, float size = 1.0f);
    // FUN_0073e840: the time of each creature of the creature list for a
    // frame of `frame_time` seconds, and the sleeping bit (1) of its flags.
    void schedule(float frame_time, const std::vector<std::array<float, 3>>& viewers);

    // What the native part of the class of an object does besides its
    // scripts (the sensor of a detectorscript, docs/SCRIPTS.md). `start`
    // runs when the object starts, after its fields are linked and before
    // its start body: the virtual +8 of a creature's object
    // (FUN_00762fe0) calls its slot +0x24 and then FUN_00580730. `update`
    // runs after the object ran in an update, with the same time:
    // FUN_0070ee70 hands the update to the object's slot +0x1c after
    // FUN_0070e8b0 ran it. An empty function is skipped. A ScriptError from
    // a hook stops the object as an error of its scripts does.
    struct ObjectHooks {
        std::function<void(std::uint32_t creature, ScriptObjectId object)> start;
        std::function<void(std::uint32_t creature, ScriptObjectId object, float time)> update;
    };
    // The hooks run in the order they were added.
    void add_object_hooks(ObjectHooks hooks) { hooks_.push_back(std::move(hooks)); }

    // FUN_00718290: each creature of the list not activated yet, in order,
    // has its object activated (FUN_0070e0c0): its fields linked, the start
    // hooks, then its start.
    void activate();
    // One frame (FUN_0072edba, FUN_00718490): the tasks of the script
    // manager with `step`; each creature of process_msg, in build order,
    // that does not sleep (bit 1 of the flags) nor hibernate gets the
    // update with its time when that time is above 0 (FUN_0070e8b0) and
    // then the update hooks; then
    // the creatures removed, sent to hibernate and woken in the frame; every
    // 60 frames the creatures with flag 0x10000 whose object has no main
    // thread leave process_msg (FUN_00718340).
    void update(float step);

    const std::vector<Creature>& creatures() const { return creatures_; }
    Creature& creature(std::uint32_t word);
    // A script error of an object (a native the port has not bound yet)
    // stops that object and is kept here; the original has no such case.
    const std::vector<std::string>& errors() const { return errors_; }

    // ScriptCreatureHost.
    std::uint32_t find_creature(std::uint32_t owner, const std::string& name) override;
    std::string creature_name(std::uint32_t creature) override;
    ScriptObjectId creature_object(std::uint32_t creature) override;
    std::uint32_t kain() override;
    ScriptObjectId hud_object() override { return 0; }
    std::array<float, 3> creature_position(std::uint32_t creature) override;
    std::array<float, 3> creature_rotation(std::uint32_t creature) override;
    bool creature_has_package(std::uint32_t creature) override;
    void hibernate_creature(std::uint32_t creature) override;
    void unhibernate_creature(std::uint32_t creature) override;
    void remove_creature(std::uint32_t creature) override;
    std::uint32_t creature_flags(std::uint32_t creature) override;
    void set_creature_flags(std::uint32_t creature, std::uint32_t flags) override;
    std::uint32_t drawn_frame(std::uint32_t creature) override;
    std::uint32_t frame() override { return frame_; }
    void leave_group(std::uint32_t creature, Group group) override;
    void set_model_lod(bool on) override { model_lod_ = on; }
    bool model_lod() const { return model_lod_; }

private:
    void stop_object(Creature& creature, const std::string& error);
    void move(std::vector<std::uint32_t>& from, std::vector<std::uint32_t>& to, std::uint32_t word);

    // FUN_0072c450: the second mode runs when the screenCullData of the
    // keeper is 1 (its settings object DAT_00a83778 is not read).
    bool second_mode() const { return has_keeper_ && keeper_.screen_cull == 1; }

    ScriptMachine& machine_;
    CrtRandom& random_;
    KeeperTune keeper_;
    bool has_keeper_ = false;
    std::uint32_t refresh_ = 0;                // DAT_00a83c94
    std::vector<Creature> creatures_;          // word = index + 1, in build order
    std::vector<std::uint32_t> active_;        // DAT_00a836a8
    std::vector<std::uint32_t> hibernating_;   // DAT_00a83680
    std::vector<std::uint32_t> to_remove_;     // DAT_00a83698
    std::vector<std::uint32_t> to_hibernate_;  // DAT_00a83670
    std::vector<std::uint32_t> to_wake_;       // DAT_00a83668
    std::uint32_t frame_ = 0;                  // DAT_008ff180
    bool model_lod_ = false;                   // DAT_00a79e75
    std::vector<std::string> errors_;
    std::vector<ObjectHooks> hooks_;
};

// The KeeperTune fields of the `levelkeeper` tunedata entry of code.big
// (docs/FORMATS/tunedata.md: a field at blob offset k is at byte 4 + k).
// Throws common::ParseError when the blob is shorter than 0x40 bytes.
ScriptLevel::KeeperTune read_keeper_tune(const std::vector<std::uint8_t>& tunedata);

// The scripts of a level container: its `bpackage` entries in entry order,
// then the package of its one region, linked into one program (docs/
// SCRIPTS.md, "Loading and linking"), and the entity tables of the region.
struct LevelScripts {
    std::vector<std::unique_ptr<ScriptPackage>> packages;
    std::string region_name;
    std::vector<RegionEntityTable> tables;
    std::unique_ptr<ScriptProgram> program;
};
// Throws ScriptError when the container does not hold exactly one region.
LevelScripts load_level_scripts(const BigArchive& archive);

}  // namespace bo2::engine
