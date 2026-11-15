# Data coverage

`bo2nx_check_data` opens every `.big` container of the game, counts its
entries by type and runs the reader of the port over each entry of the types
the port knows. The failures are the input of the next format work: this page
records what the readers reject, not what they should return.

## Running

```sh
BO2NX_ASSETS_DIR=$PWD/external/installed
./build/linux-debug/bo2nx_check_data "$BO2NX_ASSETS_DIR/media/gogames/bo2" \
  --failures /tmp/check_failures.tsv
```

The tool prints `containers <n>`, one `type <name> entries <n> checked <n>
failed <n>` line per entry type in alphabetical order, and up to 20
`fail <type> <container>:<entry>: <message>` lines per type. With
`--failures` it writes every failure to a file, one per line: container,
type, entry and message, separated by tabs.

A container that does not open is printed as `load-fail` and makes the exit
code 1. A reader failure does not change the exit code. Without arguments the
exit code is 2.

## Cell group baseline

`--cell-groups <file>` writes the load numbers of every cell group the reader
accepts, one line per group: the container path relative to the directory the
tool reads, the cell group name, `model_count`, `skipped_models`, the number
of triangles and the number of vertices, separated by tabs and sorted by
container and name.

```sh
./build/linux-debug/bo2nx_check_data "$BO2NX_ASSETS_DIR/media/gogames/bo2" \
  --cell-groups tests/data/cell_group_baseline.tsv
```

The file has 206 lines: the 236 cell groups minus the 30 the reader rejects
(see "cellgrup"). The controls are `slums/sl01` (608 models, 56 skipped,
35,053 triangles and 55,954 vertices; the host prints 49,378 and 66,143
because it adds the entity geometry) and `ui/hud` (42 models, 192 triangles
and 277 vertices). `test_cell_group_baseline` in
`tests/unit/test_model_asset.cpp` reads the file and compares the four
numbers; to stay under its 60-second budget it checks the four cell groups of
`code.big` and the nine of the slums levels, and the rest of the file is
checked by the tool. Regenerate the file when a reader change is deliberate.

## Reads of 2026-11-15 (texture row updated after BO2NX-118)

| Type | Entries | Checked | Failed |
|---|---|---|---|
| animatio | 4,081 | 4,081 | 1,925 |
| animreel | 32,791 | 32,791 | 0 |
| bpackage | 480 | 0 | 0 |
| cellgrup | 236 | 236 | 30 |
| effect | 7,865 | 0 | 0 |
| lipsync | 71,182 | 0 | 0 |
| litedata | 142 | 142 | 0 |
| liteinfo | 1 | 0 | 0 |
| model | 5,168 | 5,168 | 146 |
| musicbnk | 11 | 0 | 0 |
| planinfo | 159 | 0 | 0 |
| region | 165 | 165 | 0 |
| sample | 21,251 | 0 | 0 |
| sound | 62,073 | 0 | 0 |
| strmfile | 16,428 | 0 | 0 |
| strtable | 92 | 92 | 0 |
| texfont | 4 | 4 | 0 |
| texture | 22,483 | 22,483 | 0 |
| tunedata | 1,720 | 0 | 0 |

`animatio`, `animreel`, `cellgrup`, `litedata`, `model`, `region`, `strtable`,
`texfont` and `texture` have a registered reader. The other types are counted but not
read yet; each following issue adds the reader of one type and its row here.
The 142 `litedata` entries, the 92 `strtable` entries, the 165 `region`
entries, the 32,791 `animreel` entries, the 22,483 `texture` entries and
the four `texfont` entries are read without a failure.

## animatio

1,925 of the 4,081 entries fail, all with the same message:

| Message | Entries |
|---|---|
| `animatio: version 1 entries are alias lists, not graphs` | 1,925 |

`parse_anim_graph` reads the version 0 graphs. The version 1 entries are
alias lists, still unread. The first 20 failures:

| Container | Entry |
|---|---|
| `code.big` | `ui_hud_intent_glow:ui/hud` |
| `code.big` | `ui_hud_anim_charm_haze:ui/hud` |
| `code.big` | `ui_hud_enemy_health_bar:ui/hud` |
| `code.big` | `ui_hud_health_bar:ui/hud` |
| `code.big` | `ui_hud_lore_bar:ui/hud` |
| `code.big` | `ui_hud_rage_bar:ui/hud` |
| `code.big` | `ui_hud_letterbox:ui/hud` |
| `code.big` | `ui_hud_dg_charm_icon:ui/darkgifts` |
| `code.big` | `ui_hud_dg_fury_icon:ui/darkgifts` |
| `code.big` | `ui_hud_dg_rupture_icon:ui/darkgifts` |
| `code.big` | `ui_hud_dg_speed_icon:ui/darkgifts` |
| `code.big` | `ui_hud_dg_stealth_icon:ui/darkgifts` |
| `code.big` | `ui_hud_dg_superjump_icon:ui/darkgifts` |
| `code.big` | `ui_hud_dg_telekinesis_icon:ui/darkgifts` |
| `code.big` | `ui_hud_dg_metalring:ui/darkgifts` |
| `resources/levels/bossrooms/CharmVampireArena/cvarena.big` | `dagger_actor` |
| `resources/levels/bossrooms/CharmVampireArena/cvarena.big` | `mesh15a:bossrooms/charmvampirearena/cvarena` |
| `resources/levels/bossrooms/CharmVampireArena/cvarena.big` | `camera01:bossrooms/charmvampirearena/cvarena` |
| `resources/levels/bossrooms/CharmVampireArena/cvarena.big` | `camera02:bossrooms/charmvampirearena/cvarena` |
| `resources/levels/bossrooms/CharmVampireArena/cvarena.big` | `camera03:bossrooms/charmvampirearena/cvarena` |

## cellgrup

30 of the 236 entries fail, all with the same message:

| Message | Entries |
|---|---|
| `cell group has no cellmdls section` | 30 |

They are the cinematic cell groups and the weapon cell groups of a level:
they carry markers only, without the `cellmdls` section the reader starts
from. Reading them is still open. The first 20 failures:

| Container | Entry |
|---|---|
| `resources/levels/bossrooms/sarafanlordarena/slarena.big` | `bossrooms/sarafanlordarena/cinematics/hyldencity_1` |
| `resources/levels/bossrooms/sarafanlordarena/slarena.big` | `bossrooms/sarafanlordarena/cinematics/hyldencity_2` |
| `resources/levels/bossrooms/sarafanlordarena/slarena.big` | `bossrooms/sarafanlordarena/cinematics/hyldencity_3` |
| `resources/levels/bossrooms/sarafanlordarena/slarena.big` | `bossrooms/sarafanlordarena/cinematics/hyldencity_4` |
| `resources/levels/bossrooms/speedvampirearena/svarena.big` | `bossrooms/speedvampirearena/cinematics/speedvampire` |
| `resources/levels/canyons/cy07.big` | `canyons/cinematics/meeting_seer` |
| `resources/levels/device/devicesanctuary.big` | `device/deviceweapons` |
| `resources/levels/device/dv01.big` | `device/deviceweapons` |
| `resources/levels/device/dv02.big` | `device/deviceweapons` |
| `resources/levels/device/dv03.big` | `device/deviceweapons` |
| `resources/levels/device/dv04.big` | `device/deviceweapons` |
| `resources/levels/device/dv05.big` | `device/deviceweapons` |
| `resources/levels/device/dv06.big` | `device/deviceweapons` |
| `resources/levels/device/dv07.big` | `device/deviceweapons` |
| `resources/levels/device/dv08.big` | `device/deviceweapons` |
| `resources/levels/device/dv09.big` | `device/deviceweapons` |
| `resources/levels/device/dv10.big` | `device/deviceweapons` |
| `resources/levels/eternal prison/ep07.big` | `eternal prison/cinematics/mad_vampire` |
| `resources/levels/hyldencity/hc_01.big` | `hyldencity/cinematics/kain_arrives` |
| `resources/levels/hyldencity/hc_10.big` | `hyldencity/cinematics/calvary_arrives` |

## model

146 of the 5,168 entries fail:

| Message | Entries |
|---|---|
| `model asset has no rmd chunk` | 144 |
| `prim index count does not match the triangle count` | 2 |

The 144 are the `particles/pmarker` model of one container each: it has no
`rmd` chunk. The two others are `hyldencity/t_lamp/t_lamp` and
`hyldencity/t_lampb/t_lampb` in `hc_10.big`. The first 20 `pmarker`
failures and the two others:

| Container | Entry |
|---|---|
| `resources/levels/bossrooms/CharmVampireArena/cvarena.big` | `particles/pmarker` |
| `resources/levels/bossrooms/jumpvampirearena/jvarena.big` | `particles/pmarker` |
| `resources/levels/bossrooms/jumpvampirearena/jvarena01.big` | `particles/pmarker` |
| `resources/levels/bossrooms/madvampirearena/mvarena.big` | `particles/pmarker` |
| `resources/levels/bossrooms/sarafanlordarena/slarena.big` | `particles/pmarker` |
| `resources/levels/bossrooms/speedvampirearena/svarena.big` | `particles/pmarker` |
| `resources/levels/canyons/cy01.big` | `particles/pmarker` |
| `resources/levels/canyons/cy01a.big` | `particles/pmarker` |
| `resources/levels/canyons/cy02.big` | `particles/pmarker` |
| `resources/levels/canyons/cy02a.big` | `particles/pmarker` |
| `resources/levels/canyons/cy03.big` | `particles/pmarker` |
| `resources/levels/canyons/cy04.big` | `particles/pmarker` |
| `resources/levels/canyons/cy04a.big` | `particles/pmarker` |
| `resources/levels/canyons/cy04a1.big` | `particles/pmarker` |
| `resources/levels/canyons/cy04b.big` | `particles/pmarker` |
| `resources/levels/canyons/cy05.big` | `particles/pmarker` |
| `resources/levels/canyons/cy06.big` | `particles/pmarker` |
| `resources/levels/canyons/cy07.big` | `particles/pmarker` |
| `resources/levels/canyons/cydv.big` | `particles/pmarker` |
| `resources/levels/device/devicesanctuary.big` | `particles/pmarker` |
| `resources/levels/hyldencity/hc_10.big` | `hyldencity/t_lamp/t_lamp` |
| `resources/levels/hyldencity/hc_10.big` | `hyldencity/t_lampb/t_lampb` |

## texture

The 22,483 entries are read without a failure since the DDS reader takes
DXT2 and DXT4 (BO2NX-118). Before, the eleven DXT2 `$default` textures (one
in `code.big` and one in ten level containers) failed with
`unsupported DDS fourcc`.
