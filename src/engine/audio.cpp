#include "bo2/engine/audio.hpp"

#include <algorithm>
#include <cmath>

namespace bo2::engine {

float AudioMixer::sample_at(const PcmClip& clip, std::size_t frame, std::uint16_t channel) {
    if (frame >= clip.frames() || channel >= clip.channels) {
        return 0.0f;
    }
    return static_cast<float>(clip.samples[frame * clip.channels + channel]) / 32768.0f;
}

bool AudioMixer::mix_voice(Voice& voice, float* output, std::size_t frames,
                           std::uint32_t sample_rate, std::uint16_t channels) {
    if (!voice.clip || voice.clip->channels == 0 || voice.clip->frames() == 0 ||
        sample_rate == 0 || channels == 0) {
        return false;
    }
    const PcmClip& clip = *voice.clip;
    const double step = static_cast<double>(clip.sample_rate) / sample_rate;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (voice.position >= static_cast<double>(clip.frames())) {
            if (!voice.loop) {
                return false;
            }
            voice.position = std::fmod(voice.position, static_cast<double>(clip.frames()));
        }
        const std::size_t source = static_cast<std::size_t>(voice.position);
        const float fraction = static_cast<float>(voice.position - static_cast<double>(source));
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
            std::uint16_t source_channel = channel;
            if (clip.channels == 1) {
                source_channel = 0;
            } else if (clip.channels < channels) {
                source_channel = static_cast<std::uint16_t>(channel % clip.channels);
            }
            float sample = sample_at(clip, source, source_channel);
            if (source + 1 < clip.frames()) {
                const float next = sample_at(clip, source + 1, source_channel);
                sample += (next - sample) * fraction;
            }
            output[frame * channels + channel] += sample;
        }
        voice.position += step;
    }
    return true;
}

void AudioMixer::set_music(std::shared_ptr<const PcmClip> clip) {
    music_ = Voice{std::move(clip), 0.0, true};
}

void AudioMixer::play(std::shared_ptr<const PcmClip> clip) {
    events_.push_back(Voice{std::move(clip), 0.0, false});
}

void AudioMixer::stop_all() {
    music_ = Voice{};
    events_.clear();
}

bool AudioMixer::has_music() const {
    return music_.clip != nullptr;
}

std::size_t AudioMixer::active_events() const {
    return events_.size();
}

void AudioMixer::mix(float* output, std::size_t frames, std::uint32_t sample_rate,
                     std::uint16_t channels) {
    if (output == nullptr || frames == 0 || channels == 0) {
        return;
    }
    std::fill(output, output + frames * channels, 0.0f);
    if (music_.clip) {
        mix_voice(music_, output, frames, sample_rate, channels);
    }
    for (auto iterator = events_.begin(); iterator != events_.end();) {
        if (mix_voice(*iterator, output, frames, sample_rate, channels)) {
            ++iterator;
        } else {
            iterator = events_.erase(iterator);
        }
    }
    for (std::size_t index = 0; index < frames * channels; ++index) {
        output[index] = std::clamp(output[index], -1.0f, 1.0f);
    }
}

}  // namespace bo2::engine
