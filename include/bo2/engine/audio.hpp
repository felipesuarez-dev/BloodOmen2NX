#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace bo2::engine {

struct PcmClip {
    std::uint32_t sample_rate = 0;
    std::uint16_t channels = 0;
    std::vector<std::int16_t> samples;

    std::size_t frames() const {
        return channels == 0 ? 0 : samples.size() / channels;
    }
};

class AudioMixer {
public:
    void set_music(std::shared_ptr<const PcmClip> clip);
    void play(std::shared_ptr<const PcmClip> clip);
    void stop_all();
    bool has_music() const;
    std::size_t active_events() const;
    void mix(float* output, std::size_t frames, std::uint32_t sample_rate,
             std::uint16_t channels);

private:
    struct Voice {
        std::shared_ptr<const PcmClip> clip;
        double position = 0.0;
        bool loop = false;
    };

    static float sample_at(const PcmClip& clip, std::size_t frame, std::uint16_t channel);
    static bool mix_voice(Voice& voice, float* output, std::size_t frames,
                          std::uint32_t sample_rate, std::uint16_t channels);

    Voice music_;
    std::vector<Voice> events_;
};

}  // namespace bo2::engine
