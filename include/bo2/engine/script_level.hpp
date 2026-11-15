#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "bo2/engine/region.hpp"
#include "bo2/engine/script_machine.hpp"

namespace bo2::engine {

// The creatures of a level and their script objects (docs/SCRIPTS.md, "The
// script objects of a level", "The update of the objects of a level"): one
// creature per entity of a region, the script object of each scripted one,
// their activation in build order and their update once per frame. It is
// the creature host of the machine.
//
// Not ported yet: the simulation scheduler (FUN_0073e840), so the caller
// gives each creature its time for the frame (`time`); the creatures that
// are not entities (Kain, the cameras, the HUD) are added by the caller.
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
        bool kain = false;                    // made by the factory of "kain" (FUN_00428640)
        bool activated = false;               // FUN_0070e010
        bool hibernating = false;             // +0x160
        bool in_process = true;               // in the group process_msg
        bool in_second = true;                // in the group DAT_00a83744
        bool removed = false;                 // +0xf0 bit 0x2000: gone at the end of the frame
        bool deleted = false;                 // gone
    };

    explicit ScriptLevel(ScriptMachine& machine);
    ~ScriptLevel() override;
    ScriptLevel(const ScriptLevel&) = delete;
    ScriptLevel& operator=(const ScriptLevel&) = delete;

    // The creatures of a region, one per entity in the order of its tables,
    // with the region's package (FUN_00783900) and the class of
    // `reserved.script` (FUN_007828c0). Returns how many it added.
    std::size_t add_region(const std::string& region_name, const std::vector<RegionEntityTable>& tables);
    // A creature at the end of the creature list; returns its word (index
    // + 1). A creature with a script gets its object (FUN_0070e560): the
    // class of that name in the program, when it derives from
    // basecreaturescript; else it has none.
    std::uint32_t add_creature(Creature creature);

    // FUN_00718290: each creature of the list not activated yet, in order,
    // has its object activated (FUN_0070e0c0).
    void activate();
    // One frame (FUN_0072edba, FUN_00718490): the tasks of the script
    // manager with `step`; each creature of process_msg, in build order,
    // that does not sleep (bit 1 of the flags) nor hibernate gets the
    // update with its time when that time is above 0 (FUN_0070e8b0); then
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

    ScriptMachine& machine_;
    std::vector<Creature> creatures_;          // word = index + 1, in build order
    std::vector<std::uint32_t> active_;        // DAT_00a836a8
    std::vector<std::uint32_t> hibernating_;   // DAT_00a83680
    std::vector<std::uint32_t> to_remove_;     // DAT_00a83698
    std::vector<std::uint32_t> to_hibernate_;  // DAT_00a83670
    std::vector<std::uint32_t> to_wake_;       // DAT_00a83668
    std::uint32_t frame_ = 0;                  // DAT_008ff180
    bool model_lod_ = false;                   // DAT_00a79e75
    std::vector<std::string> errors_;
};

}  // namespace bo2::engine
