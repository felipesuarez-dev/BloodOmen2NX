#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "bo2/engine/audio.hpp"

namespace bo2::platform::sdl {

class AudioDevice {
public:
    AudioDevice();
    ~AudioDevice();
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    void start_music(std::shared_ptr<const engine::PcmClip> clip);
    void play_event(std::shared_ptr<const engine::PcmClip> clip);
    void stop();
    bool events_active() const;
    std::uint32_t sample_rate() const;
    std::uint16_t channels() const;

private:
    static void callback(void* userdata, unsigned char* stream, int length);

    std::uint32_t device_ = 0;
    std::uint32_t sample_rate_ = 44100;
    std::uint16_t channels_ = 2;
    engine::AudioMixer mixer_;
    mutable std::mutex mutex_;
    std::vector<float> scratch_;
};

}  // namespace bo2::platform::sdl
