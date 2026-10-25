# Reversing workflow

Target: `bo2.exe` (PC 1.02 Spanish GoldMaster). The original binaries and
the Ghidra project live in `external/` and are never committed.

## Tools

- Ghidra with a `x86:LE:32:default` project, headless scripts under
  `tools/ghidra/`.
- Python 3 for PE triage (`pefile` in a local venv).
- `re/FUNCTION_MAP.csv` as the inventory of recovered functions.
- Local decompiles under `re/decomp/` (gitignored).

## Policy

**Decompile before guessing.** Every behaviour or format claim needs a
`FUN_XXXXXXXX` address and a local dump. A claim without both is not
allowed in code, docs or commits. This rule exists because guessed format
decoding has already cost a full session in a sibling project.

**No verbatim code in the public repository.** The repository is public:
decompiled output is evidence, not source. It stays in `re/decomp/`
(gitignored). Commits and docs cite addresses and describe behaviour; they
do not paste decompiled functions, original strings or game text.

## Triage checklist (M0)

1. PE header: machine, timestamp, linker version, subsystem.
2. Rich header: product IDs and build number, to identify the MSVC
   toolchain.
3. Sections: names, sizes, entropy, writable/executable flags.
4. Imports: `d3d8`, `dinput`, `binkw32`, CRT, Win32.
5. Strings: paths (`data\game.erg`), error messages, registry keys,
   subsystem hints.
6. Entry point and `WinMain`; init order of subsystems.
7. Auto-analysis, then a function-recovery pass for code the analyzer
   missed (calls, jump tables, thunks).

## Function recovery

Ghidra auto-analysis misses functions reached only through indirect calls,
window procedures and callbacks. The sibling project recovered about 218
missing functions in a 190 KB binary. Expect the same here.

The recovery pass:

1. Find code references that are not defined as functions (disassemble and
   create functions).
2. Walk call graphs from `WinMain` and imported callbacks.
3. Cross-check with strings and RTTI.
4. Re-export the function list and merge it into `re/FUNCTION_MAP.csv`
   without overwriting existing rows.

## Naming

Use real names when evidence provides them: RTTI, debug strings, PDB path,
known engine classes. Otherwise use `fn_<address>` and record the evidence
that hints at the purpose. Never invent a descriptive name without
evidence.

## Ghidra project

- Project directory: `external/ghidra` (gitignored).
- Program: `bo2.exe`.
- Language: `x86:LE:32:default`.
- Analysis: default analyzers plus demangler; RTTI recovery on.

Headless helpers live in `tools/ghidra/` and are documented there. Useful
output is copied to `re/decomp/` so it survives between sessions.

## M0 triage results

Measured with `pefile` on the extracted binaries:

| Field | `bo2.exe` | `setup.exe` | `install2.dat` |
|---|---|---|---|
| Machine | `0x14c` (i386) | `0x14c` | `0x14c` |
| Linker | 6.0 | 6.0 | 6.0 |
| Timestamp | 2002-04-11 | 2002-04-10 | 2002-04-10 |
| Subsystem | 2 (GUI) | 2 (GUI) | 2 (GUI) |
| Entry point | `0x3a4674` | `0x17d0` | `0x1a3ed` |
| Image base | `0x400000` | `0x400000` | `0x400000` |
| `.text` raw | `0x43e000` | `0x4000` | `0x22000` |
| Largest section | `.text` | `.rsrc` | `.rsrc` (`0x33d000`) |
| Imports | KERNEL32, binkw32, USER32, GDI32, d3d8, DINPUT, WINMM, ole32, ADVAPI32, COMCTL32 | KERNEL32, USER32, ADVAPI32, SHELL32 | KERNEL32, USER32, GDI32, ADVAPI32, SHELL32, ole32, COMCTL32 |

`pefile.parse_rich_header()` returned no entries for these files; the
linker version is the toolchain evidence. Linker 6.0 and the 2002
timestamps match MSVC 6.0, the same toolchain family as the sibling
decompilation project. Confirm with CRT fingerprints during M0.

The PDB path embedded in `bo2.exe` is
`P:\gogames\bo2\build\bo2___Win32_Spanish_GoldMaster\bo2.pdb`. It confirms
a 32-bit MSVC build and the Spanish GoldMaster configuration; it does not
provide symbols.

## M0 installer findings

- `setup.exe` checks for DirectX 8.1 and launches `install2.dat`.
- `install2.dat` is a PE whose `.rsrc` holds an SFXI container with the
  installer script, `game.erg`, `list.txt` and UI assets.
- The script copies `bo2.exe`, `binkw32.dll`, the Bink movies and
  `game.erg` into the install tree, and marks the numbered `data/*.dat`
  chunks as `InstallType = decompress` with a target path.
- `pc.rkv` is the main archive declared by `game.erg`; it is expanded
  from the numbered chunks by the installer's decompressor.

The decompressor is the M1 gate. It lives in `install2.dat` and has not
been reverse-engineered yet.
