#include "bo2/engine/ogg.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

extern "C" {
struct stb_vorbis;
struct stb_vorbis_info {
    unsigned int sample_rate;
    int channels;
    unsigned int setup_memory_required;
    unsigned int setup_temp_memory_required;
    unsigned int temp_memory_required;
    int max_frame_size;
};
stb_vorbis* stb_vorbis_open_memory(const unsigned char* data, int len, int* error, const void* alloc);
void stb_vorbis_close(stb_vorbis* stream);
stb_vorbis_info stb_vorbis_get_info(stb_vorbis* stream);
int stb_vorbis_get_frame_short_interleaved(stb_vorbis* stream, int channels, short* buffer,
                                           int num_shorts);
int stb_vorbis_stream_length_in_samples(stb_vorbis* stream);
}

namespace bo2::engine {

namespace {

struct VorbisInfo {
    int version;
    int channels;
    long rate;
    long bitrate_upper;
    long bitrate_nominal;
    long bitrate_lower;
    long bitrate_window;
    void* codec_setup;
};

struct VorbisFileApi {
    using open_fn = int (*)(std::FILE*, void*, const char*, long);
    using info_fn = VorbisInfo* (*)(void*, int);
    using read_fn = long (*)(void*, char*, int, int, int, int, int*);
    using clear_fn = int (*)(void*);

    void* library = nullptr;
    open_fn open = nullptr;
    info_fn info = nullptr;
    read_fn read = nullptr;
    clear_fn clear = nullptr;

    VorbisFileApi() {
        library = dlopen("libvorbisfile.so.3", RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr) {
            return;
        }
        open = reinterpret_cast<open_fn>(dlsym(library, "ov_open"));
        info = reinterpret_cast<info_fn>(dlsym(library, "ov_info"));
        read = reinterpret_cast<read_fn>(dlsym(library, "ov_read"));
        clear = reinterpret_cast<clear_fn>(dlsym(library, "ov_clear"));
        if (open == nullptr || info == nullptr || read == nullptr || clear == nullptr) {
            dlclose(library);
            library = nullptr;
        }
    }

    ~VorbisFileApi() {
        if (library != nullptr) {
            dlclose(library);
        }
    }

    bool available() const { return library != nullptr; }
};

using VorbisFileStorage = std::aligned_storage<65536, alignof(std::max_align_t)>::type;

std::shared_ptr<const PcmClip> decode_vorbisfile(const std::uint8_t* data, std::size_t size) {
    static VorbisFileApi api;
    if (!api.available()) {
        throw std::runtime_error("libvorbisfile is not available");
    }
    std::FILE* file = std::tmpfile();
    if (file == nullptr) {
        throw std::runtime_error("cannot create a temporary Ogg file");
    }
    if (std::fwrite(data, 1, size, file) != size) {
        std::fclose(file);
        throw std::runtime_error("cannot write the temporary Ogg file");
    }
    std::rewind(file);
    VorbisFileStorage storage{};
    auto* vorbis = reinterpret_cast<void*>(&storage);
    if (api.open(file, vorbis, nullptr, 0) != 0) {
        std::fclose(file);
        throw std::runtime_error("libvorbisfile cannot open the stream");
    }
    VorbisInfo* info = api.info(vorbis, -1);
    if (info == nullptr || info->channels <= 0 || info->channels > 2 || info->rate <= 0) {
        api.clear(vorbis);
        throw std::runtime_error("unsupported Ogg channel count or sample rate");
    }
    auto clip = std::make_shared<PcmClip>();
    clip->sample_rate = static_cast<std::uint32_t>(info->rate);
    clip->channels = static_cast<std::uint16_t>(info->channels);
    char buffer[4096];
    for (;;) {
        int bitstream = 0;
        const long bytes = api.read(vorbis, buffer, static_cast<int>(sizeof buffer), 0, 2, 1,
                                    &bitstream);
        if (bytes == 0) {
            break;
        }
        if (bytes < 0) {
            api.clear(vorbis);
            throw std::runtime_error("libvorbisfile decode error");
        }
        if (bytes % 2 != 0) {
            api.clear(vorbis);
            throw std::runtime_error("libvorbisfile returned a partial sample");
        }
        const std::size_t count = static_cast<std::size_t>(bytes) / sizeof(std::int16_t);
        const auto* samples = reinterpret_cast<const std::int16_t*>(buffer);
        clip->samples.insert(clip->samples.end(), samples, samples + count);
    }
    api.clear(vorbis);
    if (clip->samples.empty()) {
        throw std::runtime_error("Ogg Vorbis stream has no audio");
    }
    return clip;
}


std::shared_ptr<const PcmClip> decode_stb(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0) {
        throw std::runtime_error("empty Ogg stream");
    }
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Ogg stream is too large");
    }
    int error = 0;
    stb_vorbis* stream =
        stb_vorbis_open_memory(data, static_cast<int>(size), &error, nullptr);
    if (stream == nullptr) {
        throw std::runtime_error("cannot open Ogg Vorbis stream (stb error " +
                                 std::to_string(error) + ")");
    }
    const stb_vorbis_info info = stb_vorbis_get_info(stream);
    if (info.channels <= 0 || info.channels > 2 || info.sample_rate == 0 ||
        info.max_frame_size <= 0) {
        stb_vorbis_close(stream);
        throw std::runtime_error("unsupported Ogg channel count or sample rate");
    }
    const int expected = stb_vorbis_stream_length_in_samples(stream);
    std::vector<short> pcm;
    if (expected > 0) {
        pcm.reserve(static_cast<std::size_t>(expected) * static_cast<std::size_t>(info.channels));
    }
    std::vector<short> frame(static_cast<std::size_t>(info.max_frame_size) *
                             static_cast<std::size_t>(info.channels));
    for (;;) {
        const int frames = stb_vorbis_get_frame_short_interleaved(
            stream, info.channels, frame.data(), static_cast<int>(frame.size()));
        if (frames <= 0) {
            break;
        }
        pcm.insert(pcm.end(), frame.begin(),
                   frame.begin() + static_cast<std::ptrdiff_t>(frames) * info.channels);
    }
    stb_vorbis_close(stream);
    if (pcm.empty()) {
        throw std::runtime_error("Ogg Vorbis stream has no audio");
    }
    auto clip = std::make_shared<PcmClip>();
    clip->sample_rate = static_cast<std::uint32_t>(info.sample_rate);
    clip->channels = static_cast<std::uint16_t>(info.channels);
    clip->samples = std::move(pcm);
    return clip;
}

}  // namespace

std::shared_ptr<const PcmClip> decode_ogg(const std::uint8_t* data, std::size_t size) {
    try {
        return decode_vorbisfile(data, size);
    } catch (const std::exception&) {
        return decode_stb(data, size);
    }
}

std::shared_ptr<const PcmClip> decode_ogg(const std::vector<std::uint8_t>& data) {
    return decode_ogg(data.data(), data.size());
}

}  // namespace bo2::engine
