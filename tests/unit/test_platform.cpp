#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "bo2/platform/input.hpp"
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

void test_keyboard_input() {
    const bo2::platform::InputMap map = bo2::platform::default_keyboard_map();
    bo2::platform::InputState state = map.update({bo2::platform::InputKey::W}, {});
    CHECK(state.action(bo2::platform::InputAction::MoveForward));
    CHECK(!state.action(bo2::platform::InputAction::MoveBack));

    state = map.update({bo2::platform::InputKey::S, bo2::platform::InputKey::A,
                        bo2::platform::InputKey::R, bo2::platform::InputKey::Space},
                       {});
    CHECK(state.action(bo2::platform::InputAction::MoveBack));
    CHECK(state.action(bo2::platform::InputAction::MoveLeft));
    CHECK(state.action(bo2::platform::InputAction::Run));
    CHECK(state.action(bo2::platform::InputAction::Jump));
    CHECK(!state.action(bo2::platform::InputAction::MoveForward));

    state = map.update({bo2::platform::InputKey::F, bo2::platform::InputKey::E,
                        bo2::platform::InputKey::Enter, bo2::platform::InputKey::Escape},
                       {});
    CHECK(state.action(bo2::platform::InputAction::Attack));
    CHECK(state.action(bo2::platform::InputAction::Use));
    CHECK(state.action(bo2::platform::InputAction::Menu));
    CHECK(state.action(bo2::platform::InputAction::Pause));
}

void test_switch_input() {
    const bo2::platform::InputMap map = bo2::platform::default_switch_map();
    bo2::platform::InputState state =
        map.update({}, {bo2::platform::InputButton::A, bo2::platform::InputButton::L});
    CHECK(state.action(bo2::platform::InputAction::Jump));
    CHECK(state.action(bo2::platform::InputAction::Run));

    state = map.update({}, {bo2::platform::InputButton::B, bo2::platform::InputButton::X,
                            bo2::platform::InputButton::Y, bo2::platform::InputButton::Plus,
                            bo2::platform::InputButton::Minus});
    CHECK(state.action(bo2::platform::InputAction::Attack));
    CHECK(state.action(bo2::platform::InputAction::Use));
    CHECK(state.action(bo2::platform::InputAction::Menu));
    CHECK(state.action(bo2::platform::InputAction::Pause));

    state = map.update({}, {bo2::platform::InputButton::ZL, bo2::platform::InputButton::ZR,
                            bo2::platform::InputButton::Up});
    CHECK(state.action(bo2::platform::InputAction::LookDown));
    CHECK(state.action(bo2::platform::InputAction::LookUp));
    CHECK(state.action(bo2::platform::InputAction::MoveForward));

    const bo2::platform::InputMap host = bo2::platform::default_host_map();
    state = host.update({bo2::platform::InputKey::W}, {bo2::platform::InputButton::A});
    CHECK(state.action(bo2::platform::InputAction::MoveForward));
    CHECK(state.action(bo2::platform::InputAction::Jump));
}

void test_input_axes() {
    const bo2::platform::InputMap map = bo2::platform::default_keyboard_map();
    bo2::platform::InputState state = map.update({}, {}, 0.1f, -0.5f, 0.9f, -0.9f);
    CHECK(state.move_x == 0.0f);
    CHECK(state.move_y == -0.5f);
    CHECK(state.look_x == 0.9f);
    CHECK(state.look_y == -0.9f);

    state = map.update({}, {}, 0.25f, -0.25f, 0.0f, 0.0f);
    CHECK(state.move_x == 0.25f);
    CHECK(state.move_y == -0.25f);
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
    test_keyboard_input();
    test_switch_input();
    test_input_axes();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "platform tests: ok\n";
    return 0;
}
