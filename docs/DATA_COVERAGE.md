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

## Reads of 2026-11-14

| Type | Entries | Checked | Failed |
|---|---|---|---|
| animatio | 4,081 | 0 | 0 |
| animreel | 32,791 | 0 | 0 |
| bpackage | 480 | 0 | 0 |
| cellgrup | 236 | 0 | 0 |
| effect | 7,865 | 0 | 0 |
| lipsync | 71,182 | 0 | 0 |
| litedata | 142 | 0 | 0 |
| liteinfo | 1 | 0 | 0 |
| model | 5,168 | 0 | 0 |
| musicbnk | 11 | 0 | 0 |
| planinfo | 159 | 0 | 0 |
| region | 165 | 0 | 0 |
| sample | 21,251 | 0 | 0 |
| sound | 62,073 | 0 | 0 |
| strmfile | 16,428 | 0 | 0 |
| strtable | 92 | 0 | 0 |
| texfont | 4 | 4 | 0 |
| texture | 22,483 | 22,483 | 11 |
| tunedata | 1,720 | 0 | 0 |

`texfont` and `texture` have a registered reader. The other types are
counted but not read yet; each following issue adds the reader of one type
and its row here. The four `texfont` entries are read without a failure.

## texture

11 of the 22,483 entries fail, all with the same message:

| Message | Entries |
|---|---|
| `unsupported DDS fourcc` | 11 |

They are the DXT2 `$default` texture of each container (the DDS reader takes
DXT1, DXT3 and DXT5):

| Container | Entry |
|---|---|
| `code.big` | `$default` |
| `resources/levels/eternal prison/ep10.big` | `$default` |
| `resources/levels/hyldencity/hc_01.big` | `$default` |
| `resources/levels/hyldencity/hc_05f.big` | `$default` |
| `resources/levels/keep/sk02.big` | `$default` |
| `resources/levels/smugglersden/RegionFiles/sd01.big` | `$default` |
| `resources/levels/smugglersden/RegionFiles/sd02.big` | `$default` |
| `resources/levels/smugglersden/RegionFiles/sd04.big` | `$default` |
| `resources/levels/smugglersden/RegionFiles/sd05.big` | `$default` |
| `resources/levels/uppercitynew/uc03b.big` | `$default` |
| `resources/levels/wharves/regionfiles/wh05.big` | `$default` |
