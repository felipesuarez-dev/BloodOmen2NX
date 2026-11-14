# Region entity table

Status: **entity names, models, positions and properties decoded**. The
`region` entry of each level `.big` is the placement table for scripted
entities and fixed props. Implementations: `tools/bo2fmt/region.py` and
`src/engine/region.cpp` (the host parses the table and populates its
entity world).

## Container

The `region` payload is itself a `goefile` container. Its directory uses
simple entries instead of the usual symbol-list entry:

```
char[8] type
u32     total size, including this 12-byte header
        payload
```

The payload:

```
u32 version
u32 extra
repeated section:
  char[8] name
  u32     total size, including this 12-byte header
  payload
```

The observed section names are `packages`, `entities`, `cellgrup` and
`demodata`. An `entities` section payload starts with two u32 values
(package and marker) followed by `entity` chunks:

```
char[8] "entity"
u32     total size, including this 12-byte header
u32     flags
cstr    entity name
cstr    model name
        pad to 4
Vec3    position
repeated property:
  cstr  key, padded to 4
  u32   type
  value (type 3/2: u32; type 8: cstr)
```

## Measured

Across the installed tree `region.py` extracted **15,632 entities** with
**533 distinct model names** and no parse errors. The sanctuary region
places `dagger`, `kainemitter`, `ptorch`, `l_club`, `lsword` and
`regionkeeper`; the model names are short names resolved through the
package/resource tables.

## UI scenes

`code.big` holds the user interface as four scenes, each a `region` and a
`cellgrup` entry of the same name: `ui/hud`, `ui/darkgifts`, `ui/pausemenu`
and `ui/optionscreen_01`. They use the level formats unchanged.

The in-game HUD is `ui/hud`: 38 entities over a cell group of 42 models (192
triangles, 277 vertices, 30 textures, one cell `ui_hud`). The
entity names are the ones the game looks up when it builds the HUD
(`FUN_0052ef60`): `ui_hud_health_bar`, `ui_hud_lore_bar`, `ui_hud_lore_glass`,
`ui_hud_lore_bottom`, `ui_hud_rage_bar`, `ui_hud_darkgift_icon`,
`ui_hud_letterbox`, `ui_hud_pause_fade`, `ui_hud_screen_cut` and the others.
The entity type is a template that names the widget class as its behaviour:
`assets/etypes/ui/hudhealthbar.etp` is `model ui/dummy` and
`behavior HUDHealthBar`. The classes are registered by name in
`FUN_004ab640`. The positions are in a centred screen space: the health bar
is at (-285, -138, -470) and the main frame `_main` at (-192, 176, -440).
The cell group has seven bones in all, and the container has seven `animatio`
entries named `<entity>:ui/hud`: the health, lore, rage and enemy health
bars, the intent glow, the charm haze and the letterbox.

A bar shows its value with its own clip. The health bar (`FUN_0053a9b0`)
divides the health by the maximum health and sets the time of its model's
clip to that fraction of the clip length (`FUN_00775bb0`). The clips are in
the `cellanim` section of the cell group ([`models.md`](models.md#cellanim)).
The same function scales the vial on Y by the maximum health over a value of
the Kain tuning table (`FUN_004208a0`).

The markers of the cell group tie the scene together: each one names an
object, its class, its position, its model and its clip
([`models.md`](models.md#cellmark)). Four models have no entity of their own
(`ui_hud_health_glass`, `ui_hud_health_vial`, `ui_hud_blood_drip`,
`ui_hud_blood_drop`): the bar classes look them up by name.

Not decoded yet: the projection the game draws a UI scene with and which
entities are visible outside a pause or a cutscene.

## Host

`parse_region_entities` unwraps the nested container and returns the
entity tables; `bo2nx --level` uses the region entities for its
visibility/culling diagnostics. This is the first real entity population
path for M3. The entity type (the `model` field above) names an entity
template, `assets/etypes/**/<type>.etp`, in the same container; types whose
template has `flag helperobj` are not drawn (see "Helper objects" in
[`models.md`](models.md)).

## Tools

```sh
python3 tools/bo2fmt/region.py list level.big
python3 tools/bo2fmt/region.py entities level.big --out entities.json
```

## Open questions

- The `packages` section and the short-name-to-`model` resource mapping.
- The remaining property types.
- `cellgrup`/`demodata` sections.
- Shared props such as `pilara`/`curtaina` do not appear in `region` or
  `cellinst`; their loader is still unknown (tracked as #66).
