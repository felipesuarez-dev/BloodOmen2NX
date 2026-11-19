#include "bo2/engine/script_detector.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace bo2::engine {

namespace {

// FUN_0049b981: ((float)(r % 100) * 0.02f - 1.0f) * period * 0.5f + period,
// each operation rounded to a float.
float draw_timer(std::uint32_t draw, float period) {
    const float hundredths = static_cast<float>(draw % 100U);
    return (hundredths * 0.02f - 1.0f) * period * 0.5f + period;
}

// FUN_004e1a10: a slot of the state tables of the class runs a script body.
bool has_script_event(const ScriptProgram& program, const ScriptProgramClass& cls, std::uint32_t slot) {
    for (const std::vector<std::uint32_t>& state : cls.states) {
        if (slot >= state.size() || state[slot] == 0) {
            continue;
        }
        const ScriptProgramBody& body = program.bodies()[state[slot] - 1];
        if (!body.source->native()) {
            return true;
        }
    }
    return false;
}

// One instance per machine: the natives of detectorscript are plain
// functions (FUN_004d1b60 binds them to the native part of each object).
std::map<const ScriptMachine*, DetectorSensors*>& registry() {
    static std::map<const ScriptMachine*, DetectorSensors*> instances;
    return instances;
}

}  // namespace

RangeSensor::RangeSensor(std::uint32_t mask, std::uint32_t flags)
    : mask_(mask), flags_(flags), events_(std::numeric_limits<float>::max()) {
    thresholds_ = {128.0f, 256.0f, 512.0f};
}

void RangeSensor::step(float time, const std::array<float, 3>& self, const std::string& package,
                       std::uint32_t self_creature, const std::vector<Candidate>& candidates,
                       CrtRandom& random) {
    acc_ = time + acc_;
    if (!(0.1f <= acc_)) {
        return;
    }
    if (scan_ <= 0.0f) {
        scan(self, package, self_creature, candidates, random);
        // FUN_0049bc90 draws the next scan time after the scan.
        scan_ = draw_timer(random.next(), 1.0f);
    }
    scan_ -= acc_;
    for (auto& item : entries_) {
        update_entry(item.first, item.second, acc_, self, candidates, random);
    }
    acc_ = 0.0f;
}

std::vector<RangeSensor::Event> RangeSensor::take_in_range() {
    std::vector<Event> events;
    events.swap(in_range_);
    return events;
}

bool RangeSensor::add_event_time(float time) {
    events_ = time + events_;
    if (0.25f < events_) {
        events_ = 0.0f;
        return true;
    }
    return false;
}

// FUN_0049b5f0, the range part: the creatures of the same package (or with
// none) that are in range and of a kind the mask has. A new one is asked its
// state; one that answers gets an entry and goes to the in-range list.
void RangeSensor::scan(const std::array<float, 3>& self, const std::string& package,
                       std::uint32_t self_creature, const std::vector<Candidate>& candidates,
                       CrtRandom& random) {
    std::set<std::string> seen;
    for (const Candidate& candidate : candidates) {
        if (candidate.creature == self_creature) {
            continue;
        }
        if (!(candidate.package == package || candidate.package.empty() || package.empty())) {
            continue;
        }
        if ((mask_ & (1U << candidate.kind)) == 0) {
            continue;
        }
        // FUN_00418c00: the position of the creature less the sensor's.
        const float dx = candidate.position[0] - self[0];
        const float dy = candidate.position[1] - self[1];
        const float dz = candidate.position[2] - self[2];
        // FUN_004082e0 compares (x*x + y*y) + z*z with the squared range.
        const float squared = (dx * dx + dy * dy) + dz * dz;
        if (!(squared < 1200.0f * 1200.0f)) {
            continue;
        }
        if (entries_.find(candidate.name) != entries_.end()) {
            seen.insert(candidate.name);
            continue;
        }
        // FUN_00430420: Kain answers with state 0 and +8 at 0. FUN_0047dab0,
        // the distance.
        const float distance = std::sqrt(squared);
        const std::uint32_t draw = random.next();  // drawn even when it does not answer
        if (!candidate.answers) {
            continue;
        }
        Entry entry;
        entry.creature = candidate.creature;
        entry.kind = candidate.kind;
        entry.timer = draw_timer(draw, 0.25f);
        entry.state = candidate.state;
        entry.value8 = candidate.value8;
        entry.distance = distance;
        entries_.emplace(candidate.name, entry);
        in_range_.push_back({entry.creature, candidate.name, entry.kind, entry.distance});
        seen.insert(candidate.name);
    }
    for (auto item = entries_.begin(); item != entries_.end();) {
        item = seen.count(item->first) != 0 ? std::next(item) : entries_.erase(item);
    }
}

// FUN_0049ad00, the range part: when the timer is out the state is asked
// again, the distance and the timer are taken again and the thresholds
// decide whether a copy goes to the in-range list.
void RangeSensor::update_entry(const std::string& name, Entry& entry, float acc,
                               const std::array<float, 3>& self,
                               const std::vector<Candidate>& candidates, CrtRandom& random) {
    entry.timer -= acc;
    if (!(entry.timer <= 0.0f)) {
        return;
    }
    const Candidate* candidate = nullptr;
    for (const Candidate& item : candidates) {
        if (item.creature == entry.creature) {
            candidate = &item;
            break;
        }
    }
    if (candidate == nullptr) {
        return;
    }
    const float old_distance = entry.distance;
    const std::uint32_t old_state = entry.state;
    const std::uint32_t old_value8 = entry.value8;
    entry.state = candidate->state;
    entry.value8 = candidate->value8;
    const float dx = candidate->position[0] - self[0];
    const float dy = candidate->position[1] - self[1];
    const float dz = candidate->position[2] - self[2];
    entry.distance = std::sqrt((dx * dx + dy * dy) + dz * dz);  // FUN_0047dab0
    entry.timer = draw_timer(random.next(), 0.25f);              // without taking acc off
    // 0x49b076 counts the thresholds of the new distance with the sums in the
    // other order: (z*z + y*y) + x*x.
    const float now = std::sqrt((dz * dz + dy * dy) + dx * dx);
    std::size_t before = 0;
    std::size_t after = 0;
    for (const float threshold : thresholds_) {
        before += threshold <= old_distance ? 1 : 0;
        after += threshold <= now ? 1 : 0;
    }
    if (entry.state != old_state || entry.value8 != old_value8 || before != after) {
        in_range_.push_back({entry.creature, name, entry.kind, entry.distance});
    }
}

DetectorSensors::DetectorSensors(ScriptMachine& machine, ScriptLevel& level, CrtRandom& random)
    : machine_(machine), level_(level), random_(random) {
    registry()[&machine] = this;
    // The original makes and releases the native part of the object with
    // FUN_004d1810 and FUN_004d1850, and FUN_004d1b60 binds its seven bodies
    // of detectorscript.
    machine.bind_native("detectorscript", 0, native_detector_nothing);
    machine.bind_native("detectorscript", 1, native_detector_nothing);
    machine.bind_native("detectorscript", 2, native_detector_nothing);
    machine.bind_native("detectorscript", 3, native_detector_nothing);
    machine.bind_native("detectorscript", 4, native_detector_nothing);
    machine.bind_native("detectorscript", 5, native_detector_remove_all_range_thresholds);
    machine.bind_native("detectorscript", 6, native_detector_add_range_threshold);
}

DetectorSensors::~DetectorSensors() {
    const auto found = registry().find(&machine_);
    if (found != registry().end() && found->second == this) {
        registry().erase(found);
    }
}

DetectorSensors* DetectorSensors::for_machine(const ScriptMachine& machine) {
    const auto found = registry().find(&machine);
    return found == registry().end() ? nullptr : found->second;
}

ScriptLevel::ObjectHooks DetectorSensors::hooks() {
    ScriptLevel::ObjectHooks hooks;
    hooks.start = [this](std::uint32_t creature, ScriptObjectId object) {
        start(creature, object);
    };
    hooks.update = [this](std::uint32_t creature, ScriptObjectId object, float time) {
        update(creature, object, time);
    };
    return hooks;
}

const RangeSensor* DetectorSensors::sensor_of(ScriptObjectId object) const {
    const auto found = sensors_.find(object);
    return found == sensors_.end() ? nullptr : &found->second;
}

RangeSensor* DetectorSensors::sensor_for(ScriptObjectId object) {
    const auto found = sensors_.find(object);
    return found == sensors_.end() ? nullptr : &found->second;
}

// FUN_004e13c0: the object of a detectorscript gets a sensor when its class
// has a script body for one of the four events.
void DetectorSensors::start(std::uint32_t, ScriptObjectId object) {
    const std::uint32_t class_id = machine_.class_of(object);
    if (!machine_.is_a(class_id, "detectorscript")) {
        return;
    }
    const ScriptProgram& program = machine_.program();
    const ScriptProgramClass& cls = program.classes()[class_id];
    const bool player = has_script_event(program, cls, 22);      // onplayerinrange
    const bool in_range = has_script_event(program, cls, 23);    // oncreatureinrange
    const bool insight = has_script_event(program, cls, 24);     // oncreatureinsight
    const bool left_sight = has_script_event(program, cls, 25);  // oncreatureleftsight
    if (!player && !in_range && !insight && !left_sight) {
        return;
    }
    // The original takes the mask 1 when the class has only onplayerinrange
    // and the global DAT_0091e2fc otherwise (not read, docs/SCRIPTS.md). The
    // only candidate of the port is Kain, kind 0, and the trace of sl01 shows
    // the sensors of the level sense him, so both choices agree on the only
    // kind there is.
    const std::uint32_t mask = 1;
    const std::uint32_t flags = (player || in_range ? 1U : 0U) | (insight || left_sight ? 2U : 0U);
    sensors_.insert_or_assign(object, RangeSensor(mask, flags));
}

// FUN_004e1aa0: after the object ran, the sensor steps with its time; the
// events go once its accumulator is past 0.25 s.
void DetectorSensors::update(std::uint32_t creature, ScriptObjectId object, float time) {
    const auto found = sensors_.find(object);
    if (found == sensors_.end()) {
        return;
    }
    std::vector<RangeSensor::Candidate> candidates;
    const std::uint32_t kain = level_.kain();
    if (kain != 0) {
        const ScriptLevel::Creature& other = level_.creature(kain);
        RangeSensor::Candidate candidate;
        candidate.creature = kain;
        candidate.kind = 0;  // the player, FUN_0042af90
        candidate.name = other.name;
        candidate.package = other.package;
        candidate.position = other.position;
        candidate.answers = true;  // FUN_00430420
        candidates.push_back(candidate);
    }
    const ScriptLevel::Creature& self = level_.creature(creature);
    RangeSensor& sensor = found->second;
    sensor.step(time, self.position, self.package, creature, candidates, random_);
    if (sensor.add_event_time(time)) {
        send_events(creature, object, sensor);
    }
}

// FUN_004e18a0: every copy of the in-range list calls oncreatureinrange and,
// for the player, onplayerinrange.
void DetectorSensors::send_events(std::uint32_t creature, ScriptObjectId object,
                                  RangeSensor& sensor) {
    const std::vector<RangeSensor::Event> events = sensor.take_in_range();
    for (const RangeSensor::Event& event : events) {
        call_event(creature, object, 23, event);
        if (event.kind == 0) {
            call_event(creature, object, 22, event);
        }
    }
}

// FUN_00580810 and FUN_00580a40: the body of the slot with two arguments,
// the string cell of the name (or the block of the creature) and the
// distance. Everything is given back, also when the call throws.
void DetectorSensors::call_event(std::uint32_t creature, ScriptObjectId object, std::uint32_t slot,
                                 const RangeSensor::Event& event) {
    const std::uint32_t body = machine_.body_in_slot(object, slot);
    if (body == 0) {
        return;
    }
    ScriptMemory& memory = machine_.memory();
    const ScriptAddress args = memory.allocate(8);
    ScriptAddress value = 0;
    ScriptAddress cell = 0;
    const auto give_back = [&]() {
        if (value != 0) {
            memory.release(value);
        }
        memory.release(args);
    };
    try {
        if (slot == 23) {
            cell = machine_.new_string_cell(event.name);
            memory.set_u32(args, cell);
        } else {
            value = memory.allocate(4);
            memory.set_u32(value, event.creature);
            memory.set_u32(args, value);
        }
        memory.set_f32(args + 4, event.distance);
        if (observer_) {
            observer_(creature, slot, event.creature, event.distance);
        }
        machine_.call_body(object, body - 1, 2, args, 0);
    } catch (...) {
        if (cell != 0) {
            machine_.release_string_cell(cell);
        }
        give_back();
        throw;
    }
    if (cell != 0) {
        machine_.release_string_cell(cell);
    }
    give_back();
}

// Bodies 0 to 4 (FUN_004d1c10, FUN_004d1c30, FUN_004d1cd0, FUN_004d1d70 and
// FUN_004d1e10): each calls an empty function (FUN_004e1820, FUN_004e1830,
// FUN_004e1840, FUN_004e1850 and FUN_004e1860), so they do nothing.
void native_detector_nothing(ScriptMachine&, ScriptObjectId, std::uint32_t, ScriptAddress,
                             ScriptAddress) {}

// FUN_004e1890: with no sensor nothing happens.
void native_detector_remove_all_range_thresholds(ScriptMachine& machine, ScriptObjectId self,
                                                 std::uint32_t, ScriptAddress, ScriptAddress) {
    DetectorSensors* sensors = DetectorSensors::for_machine(machine);
    if (sensors == nullptr) {
        return;
    }
    if (RangeSensor* sensor = sensors->sensor_for(self)) {
        sensor->thresholds().clear();  // FUN_0049a9f0
    }
}

// FUN_004e1870: the float of the first argument, FUN_0049aa50 adds it.
void native_detector_add_range_threshold(ScriptMachine& machine, ScriptObjectId self, std::uint32_t,
                                         ScriptAddress args, ScriptAddress) {
    DetectorSensors* sensors = DetectorSensors::for_machine(machine);
    if (sensors == nullptr) {
        return;
    }
    if (RangeSensor* sensor = sensors->sensor_for(self)) {
        sensor->thresholds().insert(machine.memory().f32(args));
    }
}

}  // namespace bo2::engine
