#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bo2::platform {

enum class LogLevel { Debug, Info, Warning, Error };

using LogSink = std::function<void(LogLevel, const std::string&)>;

void set_log_sink(LogSink sink);
void log_message(LogLevel level, const std::string& message);
const char* log_level_name(LogLevel level);

class FileSystem {
public:
    static bool exists(const std::string& path);
    static std::vector<std::uint8_t> read_binary(const std::string& path);
    static std::string read_text(const std::string& path);
    static void write_text(const std::string& path, const std::string& text);
    static void write_binary(const std::string& path, const std::vector<std::uint8_t>& data);
    static std::string join(const std::string& left, const std::string& right);
    static std::string parent(const std::string& path);
};

std::uint64_t monotonic_milliseconds();
std::uint64_t monotonic_nanoseconds();

struct MemorySnapshot {
    std::uint64_t resident_bytes = 0;
    std::uint64_t peak_resident_bytes = 0;
};

MemorySnapshot memory_snapshot();

}  // namespace bo2::platform
