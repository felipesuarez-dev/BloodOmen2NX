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
| `ScreenCut` | `ui_hud_screen_cut`, `ui_hud_letterbox` | no rule; a cut shows the entity and its end hides it | `FUN_0073d9d0` |
| `HilitComponent` | the four `ui_hud_hilit_*` | no rule; its init (`FUN_0052e6b0`) hides the entity | `FUN_0052e760`, which runs the `ScreenCut` update |
| `PauseFade` | `ui_hud_pause_fade` | set by the per-frame update | `FUN_00775b10` |
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
  `+0x48`) is set. The constructor (`FUN_007759e0`) clears it and the HUD
  sets it to its paused flag (`FUN_00531470` calls the setter at `0x775af0`):
  the pause fade is visible while the game is paused. Its init
  (`FUN_00775a50`) gives it the colour (0, 0, 0, 0.9).

A class without a visibility rule can still hide its entity:
`FUN_0070dfa0(1)` sets the byte at entity `+0x139` (and bit 0 of `+0x60`),
and the entity's draw (`FUN_0070f100`) returns at once while that byte is
set. The base init of a component (`FUN_00754df0`) clears it.

- Screen cut: a quad of 672 x 512 that covers the screen. Its init
  (`FUN_0073d8f0`) sets its three colours to (0, 0, 0, 1) and leaves it
  shown: the screen starts black. A cut (`FUN_0073da40`) is a list of keys,
  each a duration and a colour; the update (`FUN_0073d9d0`, `FUN_0073d6a0`)
  moves the colour from one key to the next in a straight line and sets it
  on the model (`FUN_00724420`, which also turns blending on while an alpha
  is below 1). When the keys run out with an alpha below 0.001 the entity is
  hidden. `FUN_0073db50` drops a running cut: colour (0, 0, 0, 0), hidden.
- Letterbox: a `ScreenCut` as well; the HUD finds it by name
  (`FUN_0052ef60`). Its model is two bars above and below the screen (y from
  250.9 to 519.7 and from -536 to -267.2 at rest). `FUN_00531840` starts a
  cut on it and moves its clip to the fraction it is given, at most 0.5.
- Highlights: the class's own setup (the code at `0x52e6e0`, which the init
  calls last) sets the colour to (1, 1, 1, 0) and hides the entity. What
  starts a highlight is not read.
- Charm haze: hidden by its init. `FUN_00535880` takes a state: 0 hides the
  entity, 1 and 2 show it. When the global at `0x91f1bc` is 0 the state goes
  to the renderer instead (slot `+0x10c`) and the entity is not touched.

The state of each entity when Kain stands at the `sl01` spawn, read from the
running game (`instances.json` of the capture `wine_hidden_3`, byte
`+0x139`): the screen cut, the charm haze and the four highlights are
hidden; the letterbox, the pause fade, `_main`, the rage bar and the intent
glow are not. The init leaves the screen cut shown, so by then a cut has
ended or has been dropped: those are the two places that hide it.

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
  `FUN_00428780` also marks stealth as owned before anything else (the call
  at `0x4288ae`) and ends by selecting gift 0 when the selected gift is not
  an owned one (`0x428f5f`).
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
- The dark gift icon (`FUN_00535e40`, `FUN_00535f10`). Its first material
  has a list of nine textures (`icon_none`, `icon_charm`, `icon_jump`,
  `icon_speed`, `icon_telekinesis`, `icon_stealth`, `icon_rupture`,
  `icon_fury`, `icon_slord`) and `FUN_005dce40(1, index, 0)` picks one: the
  index `FUN_00524cd0` gives for the type of the selected gift (charm 1,
  superJump 2, speed 3, telekinesis 4, stealth 5, rupture 6, fury 7, anything
  else 0). The colour is (1, 1, 1, 1) while the gift can be used and
  (0.5, 0.5, 0.5, 1) while it cannot.
  - Stealth can be used when the gift is enabled (byte `+9`, set by
    `FUN_00436080`) and its timer is not above 0 (`FUN_00437540`; the timer
    starts at 0, `FUN_00436390`).
  - Fury can be used when its cost is not above the rage (`FUN_00437040`).
- The model of the icon is a coin. Its first material has four triangles:
  two for the front face (z 1.08, counter-clockwise on screen) and two for
  the back face (z -1.05, clockwise, the picture mirrored), which is not
  drawn because the material culls its back faces. Its second material has
  no texture: the 36 triangles of the rim, black by the per-vertex colours of
  the model ([`FORMATS/models.md`](FORMATS/models.md#rmd-subchunks),
  `diffuse`) and edge-on while the icon does not spin.

Which gift is selected when a level starts:

- Kain is created with stealth owned and selected (`FUN_00428780`, above).
- The start record of the level then says which gifts he owns
  (`FUN_004256c0`); in the slums, fury and stealth.
- With a saved state (`FUN_0042af90`), the gift the state names (`+0x88`) is
  selected when Kain owns it, else gift 0.

Checked in the running game with the saved game the captures load (the
`sl01` checkpoint): from the first moment Kain exists the selected gift is 0,
he owns gifts 0 and 5, the rage is 0 of 450 and the saved state names gift
0. The icon in the orb of the capture is `icon_stealth`.

## Draw order and blending

The HUD is drawn by the same queue as the world
([`FORMATS/models.md`](FORMATS/models.md#render-state-and-draw-order)). Every
model of the scene has a `RenderPriority` above 0 in its `mdlattr`, so
(`FUN_0056d130`, `FUN_0056ce80`, `FUN_005611c0`):

- every piece is in the sorted pass, alpha blended (`SRCALPHA`,
  `INVSRCALPHA`), whatever the format of its texture;
- the pieces are drawn in rising order of priority, not by their z. Pieces
  with the same priority keep the order of the scene's object list.

| Priority | Objects |
|---|---|
| 8 | screen cut |
| 9 | letterbox |
| 15 | pause fade |
| 20 | charm haze |
| 30 | health, lore and rage bars, enemy health bar, blood drop |
| 40 | the glass of the health, lore and enemy vials |
| 50 | the bottom of the three vials, blood drip |
| 60 | `_main`, `_main_enemy` |
| 70 | dark gift icon, enemy icon, the weapon icons |
| 80 | `_glass`, `_enemy_glass` |
| 90 | intent glow |
| 100 | the four highlights |

A piece writes depth unless its texture is DXT5 (`FUN_0056f320`): the glass
pieces, the charm haze, the intent glow and the highlights do not. The frame,
the icons and the vial bottoms are DXT3; the bars are DXT1. The icons, the
screen cut, the letterbox, the charm haze and the pause fade cull their back
faces (`mtlctrl`); the other pieces do not.

For the ten objects the port draws, rising priority and far-to-near z are the
same sequence: rage bar (z -475), health and lore bars (-470), vial glasses
(-460), vial bottoms (-450), `_main` (-440), dark gift icon (-400), `_glass`
(-360). They differ for objects the port does not draw yet: the intent glow
(z -430, priority 90, so over `_glass`), the pause fade (z -490, priority 15)
against the letterbox (z -484.1, priority 9), and the blood drip (z -440,
priority 50) against `_main` (z -440, priority 60).

## Port

`HudScene` (`src/game/hud_scene.cpp`) takes the scene, Kain's counters
(`KainVitals`), the tuning and the mode, and returns the objects to draw with
their position, scale and colour: `_main`, `_glass`, the three pieces of
each of the two bars and the rage bar. The port starts the gift object as its
constructor leaves it, with no gift selected, which is not what the original
has when a level starts (stealth). The rage bar is the same either way: no
rage, so its clip is at its start, and `nonRageGiftBar` as its colour, which
is also the colour of a gift that spends no rage.

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

- The draw order by `RenderPriority`: the port orders by z, which gives the
  same sequence for the objects it draws (see "Draw order and blending").
  The blend is the original's.
- The dark gift icon. Its rule is read (see "Rage and dark gifts"); the
  port lacks the stealth start state, the texture list of a material and the
  culling of back faces.
- What changes the rage (blocks, dodges, the drain of `FUN_004300d0`) and
  the selection and ownership of the gifts.
- The enemy health bar and the enemy icon (mode 3 only), the pause fade
  (paused only), the charm haze and the highlights (hidden until something
  starts them), the screen cut and the letterbox (nothing starts a cut in
  the port), the blood drip of the health vial (`FUN_00532f30`) and the
  awareness glow.
- How a model uses the colour its class sets (the port multiplies the
  texture by it). `FUN_005dce00` hands parameter 8 to an object of the model
  whose class is not read.
- The depth test between pieces and the culling of back faces.
- The growth of the shown health and lore toward their targets
  (`FUN_004210b0`).
