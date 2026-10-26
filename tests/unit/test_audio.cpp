#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "bo2/engine/audio.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

std::shared_ptr<bo2::engine::PcmClip> make_clip(std::uint32_t rate, std::uint16_t channels,
                                                std::vector<std::int16_t> samples) {
    auto clip = std::make_shared<bo2::engine::PcmClip>();
    clip->sample_rate = rate;
    clip->channels = channels;
    clip->samples = std::move(samples);
    return clip;
}

void test_one_shot() {
    auto clip = make_clip(8000, 1, {1000, -1000, 2000, -2000});
    bo2::engine::AudioMixer mixer;
    mixer.play(clip);
    CHECK(mixer.active_events() == 1);
    std::vector<float> output(8);
    mixer.mix(output.data(), 4, 8000, 1);
    CHECK(std::fabs(output[0] - 1000.0f / 32768.0f) < 1.0e-6f);
    CHECK(std::fabs(output[3] + 2000.0f / 32768.0f) < 1.0e-6f);
    mixer.mix(output.data(), 4, 8000, 1);
    CHECK(mixer.active_events() == 0);
    for (float value : output) {
        CHECK(std::fabs(value) < 1.0e-6f);
    }
}

void test_music_loops() {
    auto clip = make_clip(8000, 1, {1000, 2000});
    bo2::engine::AudioMixer mixer;
    mixer.set_music(clip);
    CHECK(mixer.has_music());
    std::vector<float> output(8);
    mixer.mix(output.data(), 8, 8000, 1);
    CHECK(std::fabs(output[0] - 1000.0f / 32768.0f) < 1.0e-6f);
    CHECK(std::fabs(output[2] - 1000.0f / 32768.0f) < 1.0e-6f);
    CHECK(std::fabs(output[5] - 2000.0f / 32768.0f) < 1.0e-6f);
}

void test_stereo_and_resample() {
    auto clip = make_clip(4000, 2, {1000, -1000, 3000, -3000});
    bo2::engine::AudioMixer mixer;
    mixer.play(clip);
    std::vector<float> output(8);
    mixer.mix(output.data(), 4, 8000, 2);
    CHECK(std::fabs(output[0] - 1000.0f / 32768.0f) < 1.0e-6f);
    CHECK(std::fabs(output[1] + 1000.0f / 32768.0f) < 1.0e-6f);
    CHECK(std::fabs(output[4] - 3000.0f / 32768.0f) < 1.0e-3f);
}

void test_stop_and_clamp() {
    auto clip = make_clip(8000, 1, {32767, 32767});
    bo2::engine::AudioMixer mixer;
    mixer.set_music(clip);
    mixer.play(clip);
    std::vector<float> output(4);
    mixer.mix(output.data(), 2, 8000, 1);
    CHECK(output[0] <= 1.0f);
    CHECK(output[0] >= 0.99f);
    mixer.stop_all();
    CHECK(!mixer.has_music());
    CHECK(mixer.active_events() == 0);
}

}  // namespace

int main() {
    test_one_shot();
    test_music_loops();
    test_stereo_and_resample();
    test_stop_and_clamp();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "audio tests: ok\n";
    return 0;
}
