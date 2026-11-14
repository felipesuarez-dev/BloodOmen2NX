# HUD

Status: **partially ported**. The projection, the visibility rule and the
health and lore bars are decompiled and ported (`src/game/hud_scene.cpp`),
and both renderers draw them with `--hud`: the software renderer over a
`--level` frame and the SDL backend in the `--play` window. Every rule below
names the function it was read from; the dumps are local (`re/decomp/hud/`).

The scene data (the `ui/hud` region and cell group of `code.big`, its
markers and its clips) is described in
[`FORMATS/region.md`](FORMATS/region.md#ui-scenes) and
[`FORMATS/models.md`](FORMATS/models.md#cellmark).

## Screen space

The view object (`FUN_00752fd0`) owns two cameras: one for the world
(`FUN_005c0fa0(60 degrees, 4 / 3, 4, 4000)`) and one for the UI
(`FUN_005c0fa0(0, 0, 0, 1000)`: field of view 0, near 0, far 1000). Each
frame `FUN_00753bc0` draws the world scene with the first and the UI scene
with the second (`FUN_007f6db0`).

The renderer's camera setup (`FUN_00567090`) builds an orthographic frustum
when the field of view is 0: x from -320 to 320, y from -240 to 240, scaled
by `(2 / 640, 2 / 480, -1 / (far - near))`. So a UI scene is a 640 x 480
screen centred on the origin with y up, and an object at z = -440 has depth
0.44. The positions of the scene's objects are in that space: the frame
`_main` is at (-192, 176, -440), the top left of the screen.

## Objects and classes

Every object of the scene is a component. The class comes from the entity
template (`behavior HUDHealthBar`); the classes are registered in
`FUN_004ab640`. A component is created visible (`FUN_00754a00`) and is drawn
when it and all its parents are visible. The HUD (`FUN_0052ef60`) looks up
the objects it drives by name and then adds every other object of the scene
as a plain component. Three methods of a class matter here: the init, the
visibility update and the per-frame update.

| Class | Objects | Visibility | Per frame |
|---|---|---|---|
| `KainModeHUDComponent` | `_main`, `_glass`, `ui_hud_lore_glass`, `ui_hud_lore_bottom` | `FUN_00540b50` | nothing |
| `HUDHealthBar` | `ui_hud_health_bar`; creates `ui_hud_health_glass` and `ui_hud_health_vial` as children (`FUN_0053a2e0`) | `FUN_0053a630`, which ends in `FUN_00540b50` | `FUN_0053a9b0` |
| `HUDLoreBar` | `ui_hud_lore_bar` | `FUN_00540b50` | `FUN_0053cab0` |
| `HUDRageBar` | `ui_hud_rage_bar` | `FUN_00540b50` | `FUN_0053d180` |
| `HUDDarkGiftIcon` | `ui_hud_darkgift_icon` | `FUN_00535e40`, which ends in `FUN_00540b50` | `FUN_00535f10` |
| `HUDWeaponSubIcon` | the 17 `ui_hud_weapon_*` | set by the per-frame update | `FUN_0053f1a0` |
| `EnemyModeHUDComponent` | `_main_enemy`, `_enemy_glass`, `ui_hud_enemy_health_glass`, `ui_hud_enemy_health_bottom` | `FUN_00524d40` | nothing |
| `HUDAwarenessGlow` | `ui_hud_intent_glow` | `FUN_00532700` | `FUN_00532750` |
| `HUDCharmHaze` | `ui_hud_anim_charm_haze` | not read | `FUN_00535830` |
| `HilitComponent` | the four `ui_hud_hilit_*` | not read | `FUN_0052e760` |
| `ScreenCut`, `PauseFade`, `HUDEnemyHealthBar`, `HUDEnemyIcon` | screen cut, letterbox, pause fade, enemy bar and icon | not read | not read |

## Visibility

The HUD has a mode (`FUN_00531030`). It starts at 0. The game UI sets it to
1 when it enters the in-game state and back to 0 when it leaves it
(`FUN_0052cdb0`); mode 3 adds the enemy objects. The HUD also has a paused
flag (`FUN_00531470`).

- `FUN_00540b50`, the rule of every Kain mode class: visible when the player
  exists, the mode is 1 or 3 and the game is not paused.
- `FUN_00524d40`, the enemy mode rule: the same with mode 3 only.
- `FUN_0053f1a0`: a weapon icon is visible only while Kain holds the weapon
  it is named after.

## Bars

The health bar (`FUN_0053a9b0`) and the lore bar (`FUN_0053cab0`) follow the
same rule. With `fraction = value / maximum` and
`length = maximum / highest maximum`:

1. `FUN_00775bb0(fraction)` sets the time of the bar's clip to that fraction
   of its length. The clip scales the bar's bone in Y from 1 to 140 (health)
   or 133 (lore); the bar's model is 2 units tall. This is the fill.
2. The bar's matrix is scaled in Y by `length` (`FUN_00693340`) and moved to
   `bar position * length + glass position * (1 - length)`
   (`FUN_006930c0`).
3. The glass is scaled in Y by `length` around its own position, which is
   its top end.
4. The bottom piece (`ui_hud_health_vial`, `ui_hud_lore_bottom`) is moved to
   `its position * length + glass position * (1 - length)`: the lower end of
   the shortened glass.

The values come from Kain's health and lore counters and the highest maximum
from the last lore level of the tuning data (`FUN_004208a0` for the health,
500; `FUN_00421020` for the lore, 950), see
[`FORMATS/tunedata.md`](FORMATS/tunedata.md#healthparams-b0x898). When Kain
enters the slums the health vial is 30 / 500 of its full length and the lore
vial 6 / 950.

The health bar also sets its colour (`FUN_00775cb0`): (0.5, 0.5, 0.5, 1)
while the fraction is 0.3 or more, (1, 1, 1, 1) below it.

The rage bar (`FUN_0053d180`) sets its clip to `rage / maximum rage` (the
clip scales X from 1 to 28) and takes one of two colours of the selected
dark gift, by whether the rage covers the gift's cost. The dark gift icon
(`FUN_00535e40`, `FUN_00535f10`) shows the texture of the selected gift and
dims it when the gift cannot be used.

## Port

`HudScene` (`src/game/hud_scene.cpp`) takes the scene, Kain's counters
(`KainVitals`), the tuning and the mode, and returns the objects to draw with
their position, scale and colour: `_main`, `_glass` and the three pieces of
each of the two bars.

`bo2nx --level ... --hud <code.big>` draws them over the level frame
(`draw_screen_items` in the software renderer): each texel is multiplied by
the object's colour and blended by its alpha, and the objects are drawn from
the farthest z to the nearest. Kain's counters are the ones he enters the
level with: the port passes the folder of the level archive (`slums`) as the
level name of `FUN_004256c0`. The result against the original is in
[`ORACLE_COMPARE.md`](ORACLE_COMPARE.md#hud-of-the-sl01-spawn-frame-bo2nx-75).

`bo2nx --play ... --hud <code.big>` draws the same objects every frame in the
SDL window (`draw_hud` in `src/platform/sdl/sdl_backend.cpp`) with the same
rules: the 640 x 480 orthographic screen, the texture multiplied by the
object's colour, alpha blending and the order from the farthest z to the
nearest. The HUD is in Kain mode for the whole of play and is never paused,
because the play loop has no pause, and Kain's counters stay at the values he
enters the level with: nothing changes them yet. The two renderers agree
within 1.3 of 255 (MAE) in the HUD boxes
([`ORACLE_COMPARE.md`](ORACLE_COMPARE.md#sdl-backend-against-the-software-renderer-bo2nx-89)).

Not ported:

- The blend mode and the draw order of the original (the two above are the
  port's).
- The rage bar and the dark gift icon: the port has no rage and no dark gift
  selection.
- The objects whose visibility is not read (screen cut, letterbox, pause
  fade, charm haze, highlights, enemy bar and icon), the blood drip of the
  health vial (`FUN_00532f30`) and the awareness glow.
- How a model uses the colour its class sets (the port multiplies the
  texture by it).
- The growth of the shown health and lore toward their targets
  (`FUN_004210b0`).
