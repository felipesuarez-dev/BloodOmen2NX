#include "bo2/platform/sdl/audio.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

#include <SDL.h>

namespace bo2::platform::sdl {

AudioDevice::AudioDevice() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        throw std::runtime_error(std::string("SDL audio init: ") + SDL_GetError());
    }
    SDL_AudioSpec desired{};
    desired.freq = 44100;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = &AudioDevice::callback;
    desired.userdata = this;
    SDL_AudioSpec obtained{};
    device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (device_ == 0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        throw std::runtime_error(std::string("SDL_OpenAudioDevice: ") + SDL_GetError());
    }
    sample_rate_ = static_cast<std::uint32_t>(obtained.freq);
    channels_ = static_cast<std::uint16_t>(obtained.channels);
    scratch_.resize(static_cast<std::size_t>(obtained.samples) * channels_);
    SDL_PauseAudioDevice(device_, 0);
}

AudioDevice::~AudioDevice() {
    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void AudioDevice::start_music(std::shared_ptr<const engine::PcmClip> clip) {
    std::lock_guard<std::mutex> lock(mutex_);
    mixer_.set_music(std::move(clip));
}

void AudioDevice::play_event(std::shared_ptr<const engine::PcmClip> clip) {
    std::lock_guard<std::mutex> lock(mutex_);
    mixer_.play(std::move(clip));
}

void AudioDevice::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    mixer_.stop_all();
}

bool AudioDevice::events_active() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mixer_.active_events() != 0;
}

std::uint32_t AudioDevice::sample_rate() const {
    return sample_rate_;
}

std::uint16_t AudioDevice::channels() const {
    return channels_;
}

void AudioDevice::callback(void* userdata, unsigned char* stream, int length) {
    auto* self = static_cast<AudioDevice*>(userdata);
    if (self == nullptr || stream == nullptr || length <= 0 || self->channels_ == 0) {
        return;
    }
    const std::size_t bytes_per_frame = sizeof(std::int16_t) * self->channels_;
    const std::size_t frames = static_cast<std::size_t>(length) / bytes_per_frame;
    std::lock_guard<std::mutex> lock(self->mutex_);
    if (self->scratch_.size() < frames * self->channels_) {
        self->scratch_.resize(frames * self->channels_);
    }
    self->mixer_.mix(self->scratch_.data(), frames, self->sample_rate_, self->channels_);
    auto* output = reinterpret_cast<std::int16_t*>(stream);
    for (std::size_t index = 0; index < frames * self->channels_; ++index) {
        output[index] = static_cast<std::int16_t>(
            std::clamp(self->scratch_[index], -1.0f, 1.0f) * 32767.0f);
    }
}

}  // namespace bo2::platform::sdl
