#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "bo2/engine/crt_random.hpp"
#include "bo2/engine/script_level.hpp"
#include "bo2/engine/script_machine.hpp"

namespace bo2::engine {

// The range part of the sensor of a detectorscript (docs/SCRIPTS.md, "The
// sensor of a detectorscript"): the creatures that come in range of the
// creature of the object and the thresholds the script sets. The sight part
// (flags 2) is not ported.
class RangeSensor {
public:
    // FUN_0049b210: the mask of the kinds it senses and the flags of what it
    // looks for (1 the range events, 2 the sight ones). The thresholds start
    // at 128, 256 and 512.
    RangeSensor(std::uint32_t mask, std::uint32_t flags);

    // The ordered set of the range thresholds (+0x70, FUN_0049a9f0 and
    // FUN_0049aa50).
    std::set<float>& thresholds() { return thresholds_; }
    const std::set<float>& thresholds() const { return thresholds_; }

    // One creature the level can sense (DAT_0091dc90, FUN_0049a910). The port
    // gives only Kain, kind 0 (FUN_0042af90).
    struct Candidate {
        std::uint32_t creature = 0;
        std::uint32_t kind = 0;
        std::string name;
        std::string package;
        std::array<float, 3> position{};
        bool answers = false;  // the state query (slot +0x54, message 0x1b)
        std::uint32_t state = 0;
        std::uint32_t value8 = 0;
    };

    // FUN_0049bc90: steps of 0.1 s, the scan when its timer is out and the
    // update of every entry. `self` is the position of the creature of the
    // sensor and `package` its script package.
    void step(float time, const std::array<float, 3>& self, const std::string& package,
              std::uint32_t self_creature, const std::vector<Candidate>& candidates,
              CrtRandom& random);

    // A copy of the list of in-range events (+0x60), put there by the scan
    // and FUN_0049ad00.
    struct Event {
        std::uint32_t creature = 0;
        std::string name;
        std::uint32_t kind = 0;
        float distance = 0.0f;
    };
    // The list of in-range events: FUN_004e18a0 walks it and empties it.
    std::vector<Event> take_in_range();

    // The accumulator of the events (+0x5c, FUN_004e1aa0): adds the step and
    // says whether they go (strictly past 0.25 s).
    bool add_event_time(float time);

    // An entry of the sensor (+0x68), keyed by the name of its creature
    // (FUN_0049c4b0).
    struct Entry {
        std::uint32_t creature = 0;
        std::uint32_t kind = 0;
        float timer = 0.0f;
        std::uint32_t state = 0;
        std::uint32_t value8 = 0;
        float distance = 0.0f;
    };
    const std::map<std::string, Entry>& entries() const { return entries_; }

    std::uint32_t mask() const { return mask_; }
    std::uint32_t flags() const { return flags_; }
    float scan_timer() const { return scan_; }       // +0x28
    float step_accumulator() const { return acc_; }  // +0x0c
    float event_accumulator() const { return events_; }

private:
    void scan(const std::array<float, 3>& self, const std::string& package,
              std::uint32_t self_creature, const std::vector<Candidate>& candidates,
              CrtRandom& random);
    void update_entry(const std::string& name, Entry& entry, float acc,
                      const std::array<float, 3>& self, const std::vector<Candidate>& candidates,
                      CrtRandom& random);

    std::uint32_t mask_ = 0;
    std::uint32_t flags_ = 0;
    std::set<float> thresholds_;
    float acc_ = 0.0f;     // +0x0c
    float scan_ = 0.0f;    // +0x28
    float events_ = 0.0f;  // +0x5c, FLT_MAX at the start
    std::map<std::string, Entry> entries_;
    std::vector<Event> in_range_;
};

// The sensors of the detectors of a level (the native part of the objects of
// a detectorscript, FUN_004e13c0). The constructor binds bodies 0 to 6 of
// detectorscript; the hooks make a sensor when the object of a creature
// starts and step it after each update.
class DetectorSensors {
public:
    DetectorSensors(ScriptMachine& machine, ScriptLevel& level, CrtRandom& random);
    ~DetectorSensors();
    DetectorSensors(const DetectorSensors&) = delete;
    DetectorSensors& operator=(const DetectorSensors&) = delete;

    ScriptLevel::ObjectHooks hooks();
    const RangeSensor* sensor_of(ScriptObjectId object) const;
    // The sensor of the natives of detectorscript (bodies 5 and 6).
    RangeSensor* sensor_for(ScriptObjectId object);
    // The instance bound to a machine, null for none: the natives are plain
    // functions and reach their sensors through it.
    static DetectorSensors* for_machine(const ScriptMachine& machine);

    // Called before each call of an event, for the tests: the creature of the
    // sensor, the slot, the sensed creature and the distance.
    void set_event_observer(std::function<void(std::uint32_t creature, std::uint32_t slot,
                                               std::uint32_t sensed, float distance)> observer) {
        observer_ = std::move(observer);
    }

private:
    void start(std::uint32_t creature, ScriptObjectId object);
    void update(std::uint32_t creature, ScriptObjectId object, float time);
    void send_events(std::uint32_t creature, ScriptObjectId object, RangeSensor& sensor);
    void call_event(std::uint32_t creature, ScriptObjectId object, std::uint32_t slot,
                    const RangeSensor::Event& event);

    ScriptMachine& machine_;
    ScriptLevel& level_;
    CrtRandom& random_;
    std::map<ScriptObjectId, RangeSensor> sensors_;
    std::function<void(std::uint32_t, std::uint32_t, std::uint32_t, float)> observer_;
};

// detectorscript bodies 0 to 4 (FUN_004d1c10 to FUN_004d1e10): nothing.
void native_detector_nothing(ScriptMachine& machine, ScriptObjectId self, std::uint32_t argc,
                             ScriptAddress args, ScriptAddress result);
// detectorscript.removeallrangethresholds(), body 5 (FUN_004d1eb0,
// FUN_004e1890, FUN_0049a9f0): empties the thresholds of the sensor.
void native_detector_remove_all_range_thresholds(ScriptMachine& machine, ScriptObjectId self,
                                                 std::uint32_t argc, ScriptAddress args,
                                                 ScriptAddress result);
// detectorscript.addrangethreshold(float), body 6 (FUN_004d1ec0,
// FUN_004e1870, FUN_0049aa50): adds the value unless it is there.
void native_detector_add_range_threshold(ScriptMachine& machine, ScriptObjectId self,
                                         std::uint32_t argc, ScriptAddress args,
                                         ScriptAddress result);

}  // namespace bo2::engine
