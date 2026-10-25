#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "bo2/platform/platform.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n';           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (false)

void test_file_system() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("bo2nx_platform_" + std::to_string(bo2::platform::monotonic_nanoseconds()));
    const std::string file = bo2::platform::FileSystem::join(root.string(), "nested/cfg.txt");
    CHECK(!bo2::platform::FileSystem::exists(file));
    bo2::platform::FileSystem::write_text(file, "hello\r\nworld");
    CHECK(bo2::platform::FileSystem::exists(file));
    CHECK(bo2::platform::FileSystem::read_text(file) == "hello\r\nworld");
    CHECK(bo2::platform::FileSystem::parent(file) ==
          bo2::platform::FileSystem::join(root.string(), "nested"));

    const std::vector<std::uint8_t> bytes = {0, 1, 2, 255};
    const std::string binary = bo2::platform::FileSystem::join(root.string(), "nested/data.bin");
    bo2::platform::FileSystem::write_binary(binary, bytes);
    CHECK(bo2::platform::FileSystem::read_binary(binary) == bytes);
    std::filesystem::remove_all(root);
}

void test_log() {
    std::vector<std::pair<bo2::platform::LogLevel, std::string>> messages;
    bo2::platform::set_log_sink(
        [&messages](bo2::platform::LogLevel level, const std::string& message) {
            messages.emplace_back(level, message);
        });
    bo2::platform::log_message(bo2::platform::LogLevel::Warning, "synthetic");
    CHECK(messages.size() == 1);
    CHECK(messages[0].first == bo2::platform::LogLevel::Warning);
    CHECK(messages[0].second == "synthetic");
    bo2::platform::set_log_sink({});
}

void test_clock() {
    const std::uint64_t before = bo2::platform::monotonic_nanoseconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const std::uint64_t after = bo2::platform::monotonic_nanoseconds();
    CHECK(after > before);
    CHECK(bo2::platform::monotonic_milliseconds() <= after / 1000000ULL + 1ULL);
}

void test_memory() {
    const bo2::platform::MemorySnapshot snapshot = bo2::platform::memory_snapshot();
#if defined(__linux__)
    CHECK(snapshot.resident_bytes > 0);
    CHECK(snapshot.peak_resident_bytes > 0);
#endif
}

}  // namespace

int main() {
    test_file_system();
    test_log();
    test_clock();
    test_memory();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "platform tests: ok\n";
    return 0;
}
