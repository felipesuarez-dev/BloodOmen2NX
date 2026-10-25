#pragma once

#include <array>
#include <cstddef>
#include <set>
#include <utility>
#include <vector>

namespace bo2::platform {

enum class InputAction {
    MoveForward,
    MoveBack,
    MoveLeft,
    MoveRight,
    LookUp,
    LookDown,
    LookLeft,
    LookRight,
    Run,
    Jump,
    Attack,
    Use,
    Menu,
    Pause,
    Count
};

enum class InputKey {
    W,
    A,
    S,
    D,
    Q,
    E,
    R,
    F,
    Up,
    Down,
    Left,
    Right,
    Space,
    Enter,
    Escape,
    Tab,
    Count
};

enum class InputButton {
    A,
    B,
    X,
    Y,
    Up,
    Down,
    Left,
    Right,
    L,
    R,
    ZL,
    ZR,
    Plus,
    Minus,
    LStick,
    RStick,
    Count
};

struct InputState {
    std::array<bool, static_cast<std::size_t>(InputAction::Count)> actions{};
    float move_x = 0.0f;
    float move_y = 0.0f;
    float look_x = 0.0f;
    float look_y = 0.0f;

    bool action(InputAction index) const {
        return actions[static_cast<std::size_t>(index)];
    }
};

class InputMap {
public:
    InputMap& bind(InputKey key, InputAction action);
    InputMap& bind(InputButton button, InputAction action);

    InputState update(const std::set<InputKey>& keys, const std::set<InputButton>& buttons,
                      float move_x = 0.0f, float move_y = 0.0f, float look_x = 0.0f,
                      float look_y = 0.0f) const;

private:
    std::vector<std::pair<InputKey, InputAction>> keys_;
    std::vector<std::pair<InputButton, InputAction>> buttons_;
};

InputMap default_keyboard_map();
InputMap default_switch_map();
InputMap default_host_map();

}  // namespace bo2::platform
