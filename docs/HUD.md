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
| `HUDCharmHaze` | `ui_hud_anim_charm_haze` | no rule; its init (`FUN_005357f0`) hides the entity | `FUN_00535830` |
| `ScreenCut` | the screen cut | no rule; its init (`FUN_0073d8f0`) shows the entity and the end of a cut hides it | `FUN_0073d9d0` |
| `HilitComponent` | the four `ui_hud_hilit_*` | no rule; its init (`FUN_0052e6b0`) runs the `ScreenCut` init | `FUN_0052e760`, which runs the `ScreenCut` update |
| `PauseFade` | the pause fade | set by the per-frame update | `FUN_00775b10` |
| `HUDEnemyHealthBar` | the enemy health bar | `FUN_004b2870`, which is `FUN_00524d40` | `FUN_00539540` |
| `HUDEnemyIcon` | the enemy icon | `FUN_00539d50`, which is `FUN_00524d40` | nothing |

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
- The enemy health bar and the enemy icon follow the enemy mode rule: their
  visibility methods only call `FUN_00524d40`.
- `FUN_00775b10`: the pause fade is visible while its own flag (component
  `+0x48`) is set. The constructor (`FUN_007759e0`) clears it; what sets it
  is not read.

A class without a visibility rule can still hide its entity:
`FUN_0070dfa0(1)` sets the byte at entity `+0x139` (and bit 0 of `+0x60`),
and the entity's draw (`FUN_0070f100`) returns at once while that byte is
set. The charm haze does it in its init. The screen cut clears the byte in
its init and sets it when a cut ends (`FUN_0073d9d0`); what a screen cut and
a highlight draw while no cut is running is not read.

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

## Rage and dark gifts

The player owns a gift object (`player + 0x128`, the object the game UI
keeps at `+0x84`): the index of the selected gift (`+4`, -1 for none), the
seven gifts (`+8`), the rage (`+0x24`) and the maximum rage (`+0x28`). The
index of a gift is also the type it reports.

| Index | Gift | Spends rage |
|---|---|---|
| 0 | stealth | no |
| 1 | rupture | yes |
| 2 | superJump | no |
| 3 | speed | yes |
| 4 | charm | no |
| 5 | fury | yes |
| 6 | telekinesis | no |

- The gift object is created with no rage, no gift selected and no gift
  owned (`FUN_00435b60`, `FUN_00436080`). `FUN_00428fe0` sets the maximum
  rage to `rageParams.maxPoints` (450) and `FUN_00428780` copies the cost and
  the two rage bar colours of fury, rupture and speed from the tuning data
  ([`FORMATS/tunedata.md`](FORMATS/tunedata.md#darkgiftparams-and-rageparams)).
- `FUN_00436bb0` adds rage up to the maximum and `FUN_00436c00` takes it
  down to 0. Each frame `FUN_004300d0` takes `pointsLostPerSec` times the
  frame time.
- The rage bar (`FUN_0053d180`) sets its clip to `rage / maximum rage` (the
  clip scales X from 1 to 28) and its colour (`FUN_00775cb0`) to one of two:
  the first while that fraction reaches `threshold / maximum rage`, the
  second below it.
  - The threshold (`FUN_00425f50`) is the cost of the selected gift when it
    is fury or speed, and the maximum rage otherwise.
  - The colours (`FUN_00425fd0`) are `canUseColor` and `cantUseColor` of the
    selected gift when it spends rage, and `nonRageGiftBar` for both
    otherwise.
- The dark gift icon (`FUN_00535e40`, `FUN_00535f10`) shows the texture
  `FUN_00524cd0` gives for the type of the selected gift (charm 1, superJump
  2, speed 3, telekinesis 4, stealth 5, rupture 6, fury 7) and dims it when
  the gift cannot be used.

Which gift is selected when a level starts (`FUN_0042af90`):

- With a saved state, the gift the state names (`+0x88`) when Kain owns it,
  else gift 0. The owned gifts come from the start record of the level
  (`FUN_004256c0`).
- Without one, the creation code selects none: `FUN_00428780` runs before the
  start record marks any gift as owned. The icon code reads the selected
  gift without testing for none, so something selects one before the HUD is
  first drawn. What does it is not found.

## Port

`HudScene` (`src/game/hud_scene.cpp`) takes the scene, Kain's counters
(`KainVitals`), the tuning and the mode, and returns the objects to draw with
their position, scale and colour: `_main`, `_glass`, the three pieces of
each of the two bars and the rage bar. The rage bar starts as the gift object
is created: no rage and no gift selected, so its clip is at its start and its
colour is `nonRageGiftBar`.

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
- The dark gift icon: its texture is the selected gift's, and the port does
  not know which gift a level starts with (see "Rage and dark gifts"). The
  oracle captures load a saved game, so the icon in their frames is the gift
  that save names.
- What changes the rage (blocks, dodges, the drain of `FUN_004300d0`) and
  the selection and ownership of the gifts.
- The enemy health bar and the enemy icon (mode 3 only), the pause fade,
  the charm haze (hidden by its init), the screen cut, the letterbox and
  the highlights (not read to the end), the blood drip of the health vial
  (`FUN_00532f30`) and the awareness glow.
- How a model uses the colour its class sets (the port multiplies the
  texture by it).
- The growth of the shown health and lore toward their targets
  (`FUN_004210b0`).
