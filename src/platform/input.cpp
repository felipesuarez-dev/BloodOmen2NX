#include "bo2/platform/input.hpp"

#include <cmath>

namespace bo2::platform {

namespace {

float deadzone(float value) {
    constexpr float threshold = 0.25f;
    if (std::fabs(value) < threshold) {
        return 0.0f;
    }
    return value;
}

}  // namespace

InputMap& InputMap::bind(InputKey key, InputAction action) {
    keys_.emplace_back(key, action);
    return *this;
}

InputMap& InputMap::bind(InputButton button, InputAction action) {
    buttons_.emplace_back(button, action);
    return *this;
}

InputState InputMap::update(const std::set<InputKey>& keys,
                            const std::set<InputButton>& buttons, float move_x, float move_y,
                            float look_x, float look_y) const {
    InputState state;
    for (const auto& binding : keys_) {
        if (keys.count(binding.first) != 0) {
            state.actions[static_cast<std::size_t>(binding.second)] = true;
        }
    }
    for (const auto& binding : buttons_) {
        if (buttons.count(binding.first) != 0) {
            state.actions[static_cast<std::size_t>(binding.second)] = true;
        }
    }
    state.move_x = deadzone(move_x);
    state.move_y = deadzone(move_y);
    state.look_x = deadzone(look_x);
    state.look_y = deadzone(look_y);
    return state;
}

InputMap default_keyboard_map() {
    InputMap map;
    map.bind(InputKey::W, InputAction::MoveForward);
    map.bind(InputKey::Up, InputAction::MoveForward);
    map.bind(InputKey::S, InputAction::MoveBack);
    map.bind(InputKey::Down, InputAction::MoveBack);
    map.bind(InputKey::A, InputAction::MoveLeft);
    map.bind(InputKey::Left, InputAction::MoveLeft);
    map.bind(InputKey::D, InputAction::MoveRight);
    map.bind(InputKey::Right, InputAction::MoveRight);
    map.bind(InputKey::R, InputAction::Run);
    map.bind(InputKey::Space, InputAction::Jump);
    map.bind(InputKey::F, InputAction::Attack);
    map.bind(InputKey::E, InputAction::Use);
    map.bind(InputKey::Enter, InputAction::Menu);
    map.bind(InputKey::Escape, InputAction::Pause);
    return map;
}

namespace {

void add_switch_bindings(InputMap& map) {
    map.bind(InputButton::A, InputAction::Jump);
    map.bind(InputButton::B, InputAction::Attack);
    map.bind(InputButton::X, InputAction::Use);
    map.bind(InputButton::Y, InputAction::Menu);
    map.bind(InputButton::L, InputAction::Run);
    map.bind(InputButton::R, InputAction::Run);
    map.bind(InputButton::ZL, InputAction::LookDown);
    map.bind(InputButton::ZR, InputAction::LookUp);
    map.bind(InputButton::Plus, InputAction::Pause);
    map.bind(InputButton::Minus, InputAction::Menu);
    map.bind(InputButton::Up, InputAction::MoveForward);
    map.bind(InputButton::Down, InputAction::MoveBack);
    map.bind(InputButton::Left, InputAction::MoveLeft);
    map.bind(InputButton::Right, InputAction::MoveRight);
}

}  // namespace

InputMap default_switch_map() {
    InputMap map;
    add_switch_bindings(map);
    return map;
}

InputMap default_host_map() {
    InputMap map = default_keyboard_map();
    add_switch_bindings(map);
    return map;
}

}  // namespace bo2::platform
