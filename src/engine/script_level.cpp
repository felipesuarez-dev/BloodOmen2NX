#include "bo2/engine/script_level.hpp"

#include "bo2/common/reader.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace bo2::engine {

namespace {

constexpr float kLargest = std::numeric_limits<float>::max();  // DAT_00877e30
constexpr float kHundredth = 0.01f;                             // _DAT_00841c08

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

}  // namespace

ScriptLevel::ScriptLevel(ScriptMachine& machine, CrtRandom& random) : machine_(machine), random_(random) {
    machine_.set_creature_host(this);
}

ScriptLevel::~ScriptLevel() {
    if (machine_.creature_host() == this) {
        machine_.set_creature_host(nullptr);
    }
}

std::size_t ScriptLevel::add_region(const std::string& region_name, const std::vector<RegionEntityTable>& tables,
                                    const ModelRadius& model_radius) {
    const std::string package = region_package_name(region_name);
    std::size_t added = 0;
    for (const RegionEntityTable& table : tables) {
        for (const RegionEntity& entity : table.entities) {
            Creature creature;
            creature.name = entity.name;
            creature.package = package;
            creature.script = reserved_script(entity);
            creature.position = {entity.position.x, entity.position.y, entity.position.z};
            creature.model_radius = model_radius ? model_radius(entity) : 0.0f;
            creature.visual = true;
            add_creature(std::move(creature));
            ++added;
        }
    }
    return added;
}

// FUN_0070e560: the object of the class of the creature's script, when that
// class derives from basecreaturescript (FUN_00580a30); its +0x2c is the
// creature.
std::uint32_t ScriptLevel::add_creature(Creature creature) {
    creature.object = 0;
    creatures_.push_back(std::move(creature));
    const auto word = static_cast<std::uint32_t>(creatures_.size());
    Creature& added = creatures_.back();
    if (!added.script.empty()) {
        const std::uint32_t id = machine_.program().find_class(added.script);
        if (id != 0 && machine_.is_a(id - 1, "basecreaturescript")) {
            try {
                added.object = machine_.create_object(id - 1);
                machine_.set_creature(added.object, word);
            } catch (const ScriptError& error) {
                errors_.push_back(added.name + ": " + error.what());
            }
        }
    }
    active_.push_back(word);
    set_distances(word, 1.0f);  // the constructor passes 1 (0x70d7ba, 0x70d7f8)
    return word;
}

void ScriptLevel::set_keeper(const KeeperTune& tune) {
    keeper_ = tune;
    has_keeper_ = true;
    for (std::uint32_t word = 1; word <= creatures_.size(); ++word) {
        set_distances(word, 1.0f);  // FUN_0072c910 passes 1 (0x72d285)
    }
}

// FUN_0073e0c0 with FUN_0073e1a0 and FUN_0073e0f0. The x87 unit of the
// original runs in single precision, so each operation rounds to a float
// (docs/SCRIPTS.md, "The simulation scheduler").
void ScriptLevel::set_distances(std::uint32_t word, float size) {
    Creature& current = creature(word);
    if (second_mode()) {
        const float model = current.model_radius > 0.0f ? current.model_radius : 24.0f;
        current.radius = std::max({model, size, current.radius, 24.0f});
        if (!current.visual) {
            current.near2 = kLargest;
            current.far2 = kLargest;
            return;
        }
        float near = 20.0f / keeper_.screen_near * current.radius * 40.0f;
        float far = 20.0f / keeper_.screen_far * current.radius * 40.0f;
        near = near < 100.0f ? 100.0f : near;
        far = far < 200.0f ? 200.0f : far;
        current.near2 = near * near;
        current.far2 = far * far;
        return;
    }
    float near = 1000000.0f;
    if (current.visual) {
        if (current.model_radius > 0.0f) {
            near = current.model_radius * 40.0f;
        }
        const float least = has_keeper_ ? keeper_.periodic_dist : kLargest;
        if (near < least) {
            current.near2 = least * least;
            return;
        }
    }
    current.near2 = near * near;
}

namespace {

// FUN_0073e330: the squared distance to the nearest viewer.
float nearest_viewer(const std::array<float, 3>& position, const std::vector<std::array<float, 3>>& viewers) {
    float nearest = kLargest;
    for (const auto& viewer : viewers) {
        const float dx = viewer[0] - position[0];
        const float dy = viewer[1] - position[1];
        const float dz = viewer[2] - position[2];
        const float distance = dy * dy + dz * dz + dx * dx;
        if (distance < nearest) {
            nearest = distance;
        }
    }
    return nearest;
}

// The time a creature within its distance gets: all of what it saved when
// `least` is 0, else only past one step (the step itself while the sum is
// below two steps, else the sum less one step).
void take_time(ScriptLevel::Creature& creature, float frame_time, float least) {
    const float sum = frame_time + creature.saved;
    creature.saved = sum;
    if (!(least < sum)) {
        creature.time = 0.0f;
    } else if (least + least <= sum) {
        creature.time = sum - least;
        creature.saved = least;
    } else {
        creature.saved = sum - least;
        creature.time = least;
    }
}

}  // namespace

void ScriptLevel::schedule(float frame_time, const std::vector<std::array<float, 3>>& viewers) {
    const float step = has_keeper_ ? 1.0f / keeper_.periodic_rate : 0.016666668f;
    const std::uint32_t frame_draw = random_.next();
    const bool scaled = frame_draw % 100 == 0;
    const bool second = second_mode();
    float radius2 = kLargest;
    bool refresh = false;
    if (second) {
        refresh = ++refresh_ > 5;
        if (refresh) {
            refresh_ = 0;
        }
    } else if (has_keeper_ && keeper_.cull_dist > 0.0f) {
        radius2 = keeper_.cull_dist * keeper_.cull_dist;
    }
    for (const std::uint32_t word : active_) {
        Creature& current = creature(word);
        if ((current.flags & 4U) != 0) {
            current.flags &= ~2U;
            current.time = frame_time;
            current.saved = 0.0f;
            continue;
        }
        float distance = current.distance;
        if (!second || refresh) {
            distance = nearest_viewer(current.position, viewers);
            if (second) {
                current.distance = distance;
            }
        }
        const float bound = second ? current.far2 : radius2;
        if (distance <= bound) {
            current.flags &= ~2U;
            float least = 0.0f;
            if (current.drawn < frame_ - 1U || current.near2 < distance) {
                least = step;
            }
            if (scaled) {
                least = (static_cast<float>(random_.next() % 10) * kHundredth + 1.0f) * least;
            }
            take_time(current, frame_time, least);
            continue;
        }
        if (!second) {
            current.flags |= 2U;
            current.saved = 0.0f;
            current.time = 0.0f;
            continue;
        }
        // Beyond the far distance: asleep until the timer runs out.
        current.wake = current.wake - frame_time;
        if (current.wake >= 0.0f) {
            current.flags |= 2U;
            current.time = 0.0f;
            current.saved = (current.flags & 0x800U) != 0 ? current.saved + frame_time : 0.0f;
            continue;
        }
        const int draw = static_cast<int>(random_.next() % 40) - 20;
        current.wake = (static_cast<float>(draw) * kHundredth + 1.0f) * 3.0f;
        current.flags &= ~2U;
        current.time = (current.flags & 0x800U) != 0 ? current.saved : kHundredth;
        current.saved = 0.0f;
    }
}

ScriptLevel::Creature& ScriptLevel::creature(std::uint32_t word) {
    if (word == 0 || word > creatures_.size()) {
        throw ScriptError("no creature " + std::to_string(word));
    }
    return creatures_[word - 1];
}

// FUN_0070e6e0 for an object that hit an error: it stops and the creature
// keeps no object.
void ScriptLevel::stop_object(Creature& creature, const std::string& error) {
    errors_.push_back(creature.name + ": " + error);
    if (creature.object != 0) {
        try {
            machine_.stop(creature.object);
        } catch (const ScriptError&) {
        }
        creature.object = 0;
    }
}

void ScriptLevel::activate() {
    const std::vector<std::uint32_t> order = active_;
    for (const std::uint32_t word : order) {
        Creature& current = creature(word);
        if (current.activated) {
            continue;
        }
        current.activated = true;
        if (current.object == 0) {
            continue;
        }
        try {
            machine_.activate(current.object, [&](const std::string& name) -> ScriptObjectId {
                const std::uint32_t found = find_creature(word, name);
                return found == 0 ? 0 : creature(found).object;
            });
        } catch (const ScriptError& error) {
            stop_object(creature(word), error.what());
        }
    }
}

void ScriptLevel::update(float step) {
    ++frame_;
    if (step > 0.0f) {
        machine_.update_tasks(float_bits(step));  // FUN_00583400, the first time scale is 1
    }
    for (std::uint32_t word = 1; word <= creatures_.size(); ++word) {
        Creature& current = creature(word);
        if (!current.in_process || current.deleted || current.hibernating || (current.flags & 2U) != 0) {
            continue;
        }
        if (!(current.time > 0.0f) || current.object == 0) {
            continue;
        }
        try {
            machine_.run(current.object, float_bits(current.time));
        } catch (const ScriptError& error) {
            stop_object(creature(word), error.what());
        }
    }
    // The end of FUN_00718490: removed creatures go, then those sent to
    // hibernate (FUN_00718970) and those woken (FUN_00718a60).
    for (const std::uint32_t word : std::exchange(to_remove_, {})) {
        Creature& gone = creature(word);
        gone.deleted = true;
        if (gone.object != 0) {
            machine_.stop(gone.object);  // FUN_0070e6e0 in the creature's end
            gone.object = 0;
        }
        active_.erase(std::remove(active_.begin(), active_.end(), word), active_.end());
        hibernating_.erase(std::remove(hibernating_.begin(), hibernating_.end(), word), hibernating_.end());
    }
    for (const std::uint32_t word : std::exchange(to_hibernate_, {})) {
        Creature& sleeper = creature(word);
        if (!sleeper.hibernating && !sleeper.deleted) {
            sleeper.hibernating = true;
            move(active_, hibernating_, word);
        }
    }
    for (const std::uint32_t word : std::exchange(to_wake_, {})) {
        Creature& waking = creature(word);
        if (waking.hibernating && !waking.deleted) {
            waking.hibernating = false;
            move(hibernating_, active_, word);
        }
    }
    // FUN_00718340, every 60 frames.
    if (frame_ % 60 == 0) {
        for (Creature& current : creatures_) {
            if (current.in_process && !current.deleted && (current.flags & 0x10000U) != 0 && current.object != 0 &&
                machine_.main_thread(current.object) == nullptr) {
                current.in_process = false;
            }
        }
    }
}

void ScriptLevel::move(std::vector<std::uint32_t>& from, std::vector<std::uint32_t>& to, std::uint32_t word) {
    from.erase(std::remove(from.begin(), from.end(), word), from.end());
    to.push_back(word);
}

// FUN_00718160: the creature of that name in the package of the owner, or
// one with no package; the active list first, then the hibernating one;
// names compare with case.
std::uint32_t ScriptLevel::find_creature(std::uint32_t owner, const std::string& name) {
    const std::string package = owner == 0 ? std::string() : creature(owner).package;
    for (const std::vector<std::uint32_t>* list : {&active_, &hibernating_}) {
        for (const std::uint32_t word : *list) {
            const Creature& candidate = creature(word);
            if (candidate.name == name && (candidate.package == package || candidate.package.empty())) {
                return word;
            }
        }
    }
    return 0;
}

std::string ScriptLevel::creature_name(std::uint32_t word) { return word == 0 ? std::string() : creature(word).name; }

ScriptObjectId ScriptLevel::creature_object(std::uint32_t word) { return word == 0 ? 0 : creature(word).object; }

// FUN_004a5290: the first creature of the list made by the factory of "kain".
std::uint32_t ScriptLevel::kain() {
    for (const std::uint32_t word : active_) {
        if (creature(word).kain) {
            return word;
        }
    }
    return 0;
}

std::array<float, 3> ScriptLevel::creature_position(std::uint32_t word) { return creature(word).position; }

std::array<float, 3> ScriptLevel::creature_rotation(std::uint32_t word) {
    throw ScriptError("the rotation of creature " + creature(word).name + " (+0xc0) is not read yet");
}

bool ScriptLevel::creature_has_package(std::uint32_t word) { return !creature(word).package.empty(); }

// FUN_00718cf0, FUN_00718d30, FUN_00718b80: the change waits for the end of
// the frame.
void ScriptLevel::hibernate_creature(std::uint32_t word) { to_hibernate_.push_back(word); }

void ScriptLevel::unhibernate_creature(std::uint32_t word) { to_wake_.push_back(word); }

void ScriptLevel::remove_creature(std::uint32_t word) {
    Creature& gone = creature(word);
    gone.flags |= 0x2000U;
    if (!gone.removed) {
        gone.removed = true;
        to_remove_.push_back(word);
    }
}

std::uint32_t ScriptLevel::creature_flags(std::uint32_t word) { return creature(word).flags; }

void ScriptLevel::set_creature_flags(std::uint32_t word, std::uint32_t flags) { creature(word).flags = flags; }

std::uint32_t ScriptLevel::drawn_frame(std::uint32_t word) { return creature(word).drawn; }

void ScriptLevel::leave_group(std::uint32_t word, Group group) {
    if (group == Group::process) {
        creature(word).in_process = false;
    } else {
        creature(word).in_second = false;
    }
}

ScriptLevel::KeeperTune read_keeper_tune(const std::vector<std::uint8_t>& tunedata) {
    const common::Reader reader(tunedata);
    if (tunedata.size() < 4 || reader.u32(0) < 0x40 || tunedata.size() < 4 + 0x40) {
        throw common::ParseError("levelkeeper tunedata is shorter than its fields");
    }
    const auto field = [&](std::size_t offset) {
        const std::uint32_t bits = reader.u32(4 + offset);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof value);
        return value;
    };
    ScriptLevel::KeeperTune tune;
    tune.cull_dist = field(0x08);
    tune.periodic_dist = field(0x10);
    tune.periodic_rate = field(0x14);
    tune.screen_cull = reader.u32(4 + 0x34);
    tune.screen_near = field(0x38);
    tune.screen_far = field(0x3c);
    return tune;
}

LevelScripts load_level_scripts(const BigArchive& archive) {
    LevelScripts scripts;
    const BigEntry* region = nullptr;
    for (const BigEntry& entry : archive.entries()) {
        if (entry.type == "bpackage") {
            scripts.packages.push_back(std::make_unique<ScriptPackage>(
                parse_script_package(archive.extract(entry), entry.symbols, archive.names())));
        } else if (entry.type == "region") {
            if (region != nullptr) {
                throw ScriptError("level container holds more than one region");
            }
            region = &entry;
        }
    }
    if (region == nullptr) {
        throw ScriptError("level container holds no region");
    }
    const std::vector<std::uint8_t> data = archive.extract(*region);
    scripts.packages.push_back(std::make_unique<ScriptPackage>(
        parse_region_script_package(data, region->symbols, archive.names())));
    scripts.region_name = region->name;
    scripts.tables = parse_region_entities(data);
    std::vector<const ScriptPackage*> linked;
    for (const auto& package : scripts.packages) {
        linked.push_back(package.get());
    }
    // The value types with no class that the references of kind 2 name
    // (docs/SCRIPTS.md, "Linking checked on the data").
    scripts.program = std::make_unique<ScriptProgram>(ScriptProgram::link(linked, {"activation_info", "creature"}));
    return scripts;
}

}  // namespace bo2::engine
