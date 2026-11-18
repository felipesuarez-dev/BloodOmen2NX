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

The file has 236 lines, one per cell group (BO2NX-166 removed the last
rejected groups). The controls are `slums/sl01` (608 models, 56 skipped,
35,053 triangles and 55,954 vertices; the host prints 49,378 and 66,143
because it adds the entity geometry) and `ui/hud` (42 models, 192 triangles
and 277 vertices). `test_cell_group_baseline` in
`tests/unit/test_model_asset.cpp` reads the file and compares the four
numbers; to stay under its 60-second budget it checks the four cell groups of
`code.big`, the nine of the slums levels and the cinematic group of `slums/sl07`
without models, and the rest of the file is checked by the tool. Regenerate
the file when a reader change is deliberate.

## Reads of 2026-11-16 (texture row updated after BO2NX-118, bpackage after BO2NX-146, animatio after BO2NX-165, cellgrup after BO2NX-166, model after BO2NX-167)

| Type | Entries | Checked | Failed |
|---|---|---|---|
| animatio | 4,081 | 4,081 | 0 |
| animreel | 32,791 | 32,791 | 0 |
| bpackage | 480 | 480 | 0 |
| cellgrup | 236 | 236 | 0 |
| effect | 7,865 | 0 | 0 |
| lipsync | 71,182 | 0 | 0 |
| litedata | 142 | 142 | 0 |
| liteinfo | 1 | 0 | 0 |
| model | 5,168 | 5,168 | 2 |
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

`animatio`, `animreel`, `bpackage`, `cellgrup`, `litedata`, `model`, `region`,
`strtable`, `texfont` and `texture` have a registered reader. The other types are counted but not
read yet; each following issue adds the reader of one type and its row here.
The 4,081 `animatio` entries, the 236 `cellgrup` entries, the 142
`litedata` entries, the 92 `strtable` entries, the 165 `region` entries (their
entities and their script package), the 32,791 `animreel` entries, the 22,483
`texture` entries and the four `texfont` entries are read without a failure.

## cellgrup

All 236 entries are read without a failure. 29 of them have no `cellmdls`
section (sixteen `cellmark` plus `attrs` plus `cellbsp`, twelve `cellmark`
plus `cells` plus `attrs` plus `cellbsp`, and `keep/sanctuary_keep` with
`attrs` plus `cellbsp`); the chunk stream starts at byte 8 and the group has
no models, and the 28 with `cellmark` hold 141 markers. The 30th,
`lowercity/cinematics/kainmeetsvorador` (`kainmeetsvorador.big`), has a model
whose `rmd` has no triangles. Both kinds are ported in BO2NX-166; the host
keeps the `cell group has no renderable geometry` error for a group it cannot
draw.

## model

2 of the 5,168 entries fail: `hyldencity/t_lamp/t_lamp` and
`hyldencity/t_lampb/t_lampb` in `hc_10.big`, both with `prim index count does
not match the triangle count`. The 144 `particles/pmarker` models, one per
container, have no `rmd` chunk; BO2NX-167 reads them as markers with their
collision and no geometry.

| Container | Entry | Message |
|---|---|---|
| `resources/levels/hyldencity/hc_10.big` | `hyldencity/t_lamp/t_lamp` | `prim index count does not match the triangle count` |
| `resources/levels/hyldencity/hc_10.big` | `hyldencity/t_lampb/t_lampb` | `prim index count does not match the triangle count` |

## texture

The 22,483 entries are read without a failure since the DDS reader takes
DXT2 and DXT4 (BO2NX-118). Before, the eleven DXT2 `$default` textures (one
in `code.big` and one in ten level containers) failed with
`unsupported DDS fourcc`.
