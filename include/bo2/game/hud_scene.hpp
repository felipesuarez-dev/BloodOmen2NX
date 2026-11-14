#pragma once

#include <cstdint>
#include <vector>

#include "bo2/engine/cell_group.hpp"
#include "bo2/engine/math.hpp"
#include "bo2/game/kain_tuning.hpp"
#include "bo2/game/kain_vitals.hpp"

namespace bo2::game {

// The screen of a UI scene. The view (FUN_00752fd0) draws it with a camera
// whose field of view is 0, near plane 0 and far plane 1000, and for such a
// camera the renderer (FUN_00567090) builds an orthographic frustum: x from
// -320 to 320, y from -240 to 240 (y up), depth = -z / (far - near).
constexpr float kHudHalfWidth = 320.0f;
constexpr float kHudHalfHeight = 240.0f;
constexpr float kHudFar = 1000.0f;

// The mode of the HUD (HUD + 0x104, FUN_00531030). The game UI sets Kain when
// it enters the in-game state and Hidden when it leaves it (FUN_0052cdb0).
enum class HudMode : std::int32_t {
    Hidden = 0,
    Kain = 1,
    KainAndEnemy = 3,
};

// One object of the scene to draw: the vertices of `model` scaled per axis
// and then moved to `position`, in the screen space above. No object of the
// HUD scene is rotated.
struct HudItem {
    std::uint32_t model = 0;  // index of the model and of its marker in the scene
    engine::Vec3 position;
    engine::Vec3 scale{1.0f, 1.0f, 1.0f};
    // The colour the object's class sets on its model (FUN_00775cb0); white
    // for a class that sets none.
    engine::Vec3 colour{1.0f, 1.0f, 1.0f};
    float alpha = 1.0f;
};

// The rules of the HUD objects whose state the port has. Drawn: the frame
// and its glass, the health bar with its glass and vial, the lore bar with
// its glass and bottom. Not drawn: the rage bar and the dark gift icon (they
// follow the selected dark gift), the weapon icons (hidden without a weapon,
// FUN_0053f1a0), the enemy mode objects (mode 3 only, FUN_00524d40) and the
// classes whose rule is not ported (screen cut, pause fade, charm haze,
// awareness glow, the highlights, the blood drip).
class HudScene {
public:
    // `scene` is the `ui/hud` cell group of code.big. Throws
    // std::invalid_argument when a model the rules name is missing or a bar
    // has no clip.
    explicit HudScene(const engine::LevelGeometry& scene);

    // The visible objects, in scene order. Empty unless the mode is Kain or
    // KainAndEnemy and the game is not paused (FUN_00540b50). Throws
    // std::invalid_argument when a maximum of `vitals` is not positive and
    // std::out_of_range when the lore level table of `tuning` is empty.
    std::vector<HudItem> items(const KainVitals& vitals, const KainTuning& tuning, HudMode mode,
                               bool paused) const;

private:
    struct Bar {
        std::uint32_t bar = 0;
        std::uint32_t glass = 0;
        std::uint32_t bottom = 0;
        engine::AnimReel clip;
    };

    void add_bar(std::vector<HudItem>& items, const Bar& bar, float fraction, float length,
                 const engine::Vec3& colour) const;

    std::vector<engine::Vec3> positions_;  // of every marker
    std::vector<std::uint32_t> frame_;     // the plain Kain mode components
    Bar health_;
    Bar lore_;
};

}  // namespace bo2::game
