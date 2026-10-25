#include "bo2/platform/platform.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__linux__)
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace bo2::platform {

namespace {

LogSink& sink() {
    static LogSink instance;
    return instance;
}

std::vector<std::uint8_t> read_bytes(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot read " + path);
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                     std::istreambuf_iterator<char>());
}

}  // namespace

void set_log_sink(LogSink replacement) { sink() = std::move(replacement); }

const char* log_level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "debug";
        case LogLevel::Info:
            return "info";
        case LogLevel::Warning:
            return "warning";
        case LogLevel::Error:
            return "error";
    }
    return "unknown";
}

void log_message(LogLevel level, const std::string& message) {
    if (sink()) {
        sink()(level, message);
        return;
    }
    std::cerr << '[' << log_level_name(level) << "] " << message << '\n';
}

bool FileSystem::exists(const std::string& path) {
    return std::filesystem::exists(path);
}

std::vector<std::uint8_t> FileSystem::read_binary(const std::string& path) {
    return read_bytes(path);
}

std::string FileSystem::read_text(const std::string& path) {
    const std::vector<std::uint8_t> bytes = read_bytes(path);
    return std::string(bytes.begin(), bytes.end());
}

void FileSystem::write_text(const std::string& path, const std::string& text) {
    const std::filesystem::path target(path);
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path());
    }
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot write " + path);
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void FileSystem::write_binary(const std::string& path,
                              const std::vector<std::uint8_t>& data) {
    const std::filesystem::path target(path);
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path());
    }
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot write " + path);
    }
    stream.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
}

std::string FileSystem::join(const std::string& left, const std::string& right) {
    return (std::filesystem::path(left) / right).string();
}

std::string FileSystem::parent(const std::string& path) {
    return std::filesystem::path(path).parent_path().string();
}

std::uint64_t monotonic_milliseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::uint64_t monotonic_nanoseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

MemorySnapshot memory_snapshot() {
    MemorySnapshot snapshot;
#if defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    if (statm) {
        std::uint64_t size_pages = 0;
        std::uint64_t resident_pages = 0;
        statm >> size_pages >> resident_pages;
        const long page_size = sysconf(_SC_PAGESIZE);
        if (page_size > 0) {
            snapshot.resident_bytes = resident_pages * static_cast<std::uint64_t>(page_size);
        }
    }
    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss > 0) {
        snapshot.peak_resident_bytes = static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
    }
#endif
    return snapshot;
}

}  // namespace bo2::platform
