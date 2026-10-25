# Video

Status: **verified** (magic only).

## Observed

The reference ISO root holds nine files that start with `BIKi`, the Bink
Video magic:

| File | Size (bytes) |
|---|---:|
| `awakens.dat` | 121,171,864 |
| `bo2intro.dat` | 32,688,664 |
| `credits.dat` | 27,130,380 |
| `crystal.dat` | 3,834,148 |
| `eidos.dat` | 8,192,104 |
| `install2.dat` | 3,579,904 |
| `outro.dat` | 34,522,168 |
| `spooky.dat` | 7,489,928 |
| `sr2_trailer.dat` | 47,713,056 |

`install2.dat` also starts with a PE header; the `BIKi` observation applies
to the movie files, not to the installer payload.

## Installation

The installer script copies these files into `data/` unchanged. The game
configuration references them by logical name (`movies\eidos`,
`movies\crystal`, `movies\bo2intro`, ...), so the engine resolves a
logical name to a `.dat` file in the data directory.

## Plan

Bink is proprietary. The port will not ship a Bink runtime. The asset
preparation step transcodes the videos to a free codec and the engine
plays the converted files. The exact codec and the conversion tool are
decided in M4, after the container work.

## Open questions

- Resolution, frame rate and audio codec of each movie.
- Whether the game streams from the file or loads it whole.
- Where the movie table lives (`game.erg` or the executable).
