# Levels

The campaign is 11 chapters, in this order: Slums, Smugglers Den, Lower City,
Upper City, Sarafan Keep, Industrial Quarter, Canyons, Eternal Prison, Device,
Wharves, Hylden City. The README progress counts one chapter as one level, and
a chapter is **completable** only when the port plays it from its entry region
to the hand-off into the next chapter (or, for the last one, to the end). The
five boss arenas are part of the chapter that opens them. Nothing is
completable yet.

## Chapter table

Source: the original's chapter table, see [Where the list comes from](#where-the-list-comes-from).
Every region name is a `.big` under `media/gogames/bo2/resources/levels/`
(lookup is case-insensitive: the table says `Keep/sk01`, the folder is `keep`).

| # | Chapter | Folder | Regions | Entry region | Hands off to | Boss arena (opened from) |
|--:|---|---|--:|---|---|---|
| 1 | Slums | `slums` | 8 (`sl01`-`sl08`) | `sl01` | `sd01` from `sl08` | none |
| 2 | Smugglers Den | `smugglersden/RegionFiles` | 7 (`sd01`-`sd07`) | `sd01` | `jvarena` from `sd07`, then `lc01` | `jvarena` (`sd07`) |
| 3 | Lower City | `lowercity` | 12 | `lc01` | cinematic `kainmeetsvorador`, then `uc01a` | none |
| 4 | Upper City | `uppercitynew` | 17 | `uc01a` | `cvarena` from `uc02a2`, then `sk01` | `cvarena` (`uc02a2`) |
| 5 | Sarafan Keep | `keep` | 10 | `sk01` | cinematic `kaintakesumahtosanctuary`, then `iq_intro` | none |
| 6 | Industrial Quarter | `industrial quarter` | 14 | `iq_intro` | `svarena` from `iq_12`, cinematic `after_iq`, then `cy01` | `svarena` (`iq_12`) |
| 7 | Canyons | `canyons` | 13 | `cy01` | `cydv` from `cy07`, then `ep01` | none |
| 8 | Eternal Prison | `eternal prison` | 11 | `ep01` | `mvarena` from `ep12`, then `dv01` | `mvarena` (`ep12`) |
| 9 | Device | `device` | 9 | `dv01` | `device/devicesanctuary`, then `wh01` | none |
| 10 | Wharves | `wharves/regionfiles` | 11 | `wh01` | `hc_01` from `wh13` | none |
| 11 | Hylden City | `hyldencity` | 17 | `hc_01` | `slarena` from `hc_07`, the end | `slarena` (`hc_07`) |

The "Regions" column counts the entries of the original table (134 in total,
the five boss arenas included). One table entry, `hyldencity/hc_04a`, has no
`.big` in the installed tree (`hc_04f` exists but is not in the table); the
other 133 do. Each chapter's regions stream into each other, so they are not
levels of their own.

## Where the list comes from

| Evidence | What it gives | Address / file |
|---|---|---|
| Chapter table in `bo2.exe` | 12 groups of (name, list of region paths): the 11 chapters above plus `boss rooms`; 134 paths; the order used here | data `0x008f0f08`-`0x008f0f68` (12 x 8 bytes: name pointer, null-terminated pointer array) |
| Level Load Menu | The table feeds a developer menu: `FUN_004a3c40` builds the menu ("Level Load Menu", next to "BO2 Most Used Flags"), `FUN_004a3860` opens one "Load Region" entry per path of the chosen group | strings `0x008f2254`, `0x008f222c` |
| Start level | `Level="slums/sl01"` | `data/game.erg` |
| Loading screens | 11 files, one per chapter and none for the boss rooms: `canyons`, `device`, `eternalprison`, `hylden`, `industrial`, `keep`, `lowercity`, `slums`, `smugden`, `uppercity`, `wharves` | `hud/load_screens/*_load.big` |
| Boss arenas | Each arena path appears inside one region of one chapter: `jvarena` in `sd07`, `cvarena` in `uc02a2`, `svarena` in `iq_12`, `mvarena` in `ep12`, `slarena` in `hc_07` | see below |
| Hand-offs | A chapter's last region (or a cinematic of its folder) contains the path of the next chapter's entry region | see below |
| Entry regions | `hubworld.big` contains the path of each chapter's entry region: `sl01`, `sd01`, `lc01`, `uc01a`, `sk01`, `iq_intro`, `cy01`, `ep01`, `dv01`, `wh01`, `hc_01` | `hubworld/hubworld.big` |

How to reproduce the hand-offs: lower-case a `.big`, replace `\` with `/`, and
search it for every path of the table. A hit is a link between two regions.
It shows that the region mentions the other, not who triggers the load or in
which direction, so the direction above also relies on the file names (a
cinematic in the `lowercity` folder that names `uc01a` leads out of the Lower
City). The scripting that fires the loads is not decoded.

Links found (chapter hand-offs only; links inside a chapter follow the region
numbers):

| From | Names |
|---|---|
| `slums/sl08` | `smugglersden/regionfiles/sd01` |
| `smugglersden/regionfiles/sd07` | `bossrooms/jumpvampirearena/jvarena` |
| `bossrooms/jumpvampirearena/jvarena01` | `lowercity/lc01`, `keep/sk01` |
| `lowercity/cinematics/kainmeetsvorador` | `uppercitynew/uc01a` |
| `uppercitynew/uc02a2` | `bossrooms/charmvampirearena/cvarena` |
| `bossrooms/charmvampirearena/cvarena` | `keep/sk01`, `uppercitynew/uc02a2` |
| `keep/cinematics/kaintakesumahtosanctuary` | `industrial quarter/iq_intro` |
| `industrial quarter/iq_12` | `bossrooms/speedvampirearena/svarena` |
| `industrial quarter/cinematics/after_iq` | `canyons/cy01` |
| `canyons/cydv` | `device/dv01`, `eternal prison/ep01`, `keep/sk01` |
| `eternal prison/ep12` | `bossrooms/madvampirearena/mvarena` |
| `bossrooms/madvampirearena/mvarena` | `device/dv01` |
| `device/devicesanctuary` | `wharves/regionfiles/wh01` |
| `wharves/regionfiles/wh13` | `hyldencity/hc_01` |
| `hyldencity/hc_07` | `bossrooms/sarafanlordarena/slarena` |

`canyons/cydv` names three regions and `device/dv10` names `cydv` and `dv01`;
which of those links the story uses is not established. The chapter order is
the table order. The links above are consistent with it at every step and none
contradicts it.

## Files that are not chapters

The installed tree has 177 level `.big` files. 133 are table entries; the other
44 are support data or are outside the table:

| Kind | Files |
|---|---|
| Music | 10: `*_music` |
| Weapons | 4: `weapons`, `smweapons`, `hc_weapons`, `whweapons` |
| Skies | `cysky`, `whsky`, `ui/mainsky` |
| Loading screens | 11 under `hud/load_screens` |
| Kain | 5 under `kain` |
| Menu | `ui/mainmenu` |
| Cinematics | `after_iq`, `kainmeetsvorador`, `kaintakesumahtosanctuary` |
| Other | `hubworld`, `sanctuary`, `device/devicesanctuary`, `device/dv10`, `hyldencity/hc_04f`, `eternal prison/ep_fakeisle`, `bossrooms/jumpvampirearena/jvarena01` |

`sanctuary` is the level the host viewer renders (`bo2nx --level`); it is not in
the table and is not counted as a chapter.

## What counts for the progress bar

`tools/gen/progress_data.py` lists the 11 chapters in this order, each with
`completable = False`. Flip one only when the port starts the chapter at its
entry region and reaches the hand-off (the end, for Hylden City) with the
original's rules, checked by a replayed route or an oracle comparison.
`tests/python/test_progress.py` keeps this document and that list in step.
