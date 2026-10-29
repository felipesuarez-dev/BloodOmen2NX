"""Progress inputs for tools/gen/progress.py.

LEVELS: the 11 campaign chapters in game order, as (name, completable). A
        chapter is completable when the port plays it from its entry region to
        the hand-off into the next chapter (the end, for the last one). The
        list, its source in bo2.exe and the region mapping are in
        docs/LEVELS.md; tests/python/test_progress.py keeps both in step.

PHASE: the milestone in progress, for the phase badge of both READMEs.

PLATFORM_FEATURES: (name, state) with state in {"done", "partial", "todo"}.

Function status rule for re/FUNCTION_MAP.csv (only bo2.exe addresses):
  translated  the port reproduces the contract of that exact function for every
              input its callers can give (a divergence on malformed data is
              allowed and written in the note), a unit test pins the behaviour
              and a local dump exists. A FUN_ address in a comment does not
              count.
  wip         the port has code for it, but only part of the contract (one
              branch, one derived value), or the whole contract without a test
              that pins it, or without an audit against the dump.
  todo        no port code.
  external    not game code: compiler unwind funclets and statically linked
              libraries (see tools/gen/README.md).
Every translated and wip row names the port file first in `notes`
("<path>: what is and is not ported"); tests/python/test_progress.py checks it.
"""

PHASE = "M3"  # the milestone in progress (ROADMAP.md); shown in the phase badge

LEVELS = [
    ("Slums", False),
    ("Smugglers Den", False),
    ("Lower City", False),
    ("Upper City", False),
    ("Sarafan Keep", False),
    ("Industrial Quarter", False),
    ("Canyons", False),
    ("Eternal Prison", False),
    ("Device", False),
    ("Wharves", False),
    ("Hylden City", False),
]

PLATFORM_FEATURES = [
    ("Host build skeleton", "done"),
    ("Installer container (SFXI)", "done"),
    ("Installer chunk decompressor", "done"),
    ("BIG/RKV parsers", "done"),
    ("Validated asset extraction", "done"),
    ("Headless host first frame", "done"),
    ("Level geometry (cellgrup)", "done"),
    ("Level textures (DDS binding)", "done"),
    # SDL2 window, GLSL 1.20 pipeline (src/platform/sdl/sdl_backend.cpp,
    # render_pipeline.cpp); the sdl_triangle ctest draws a frame under Xvfb.
    ("Host renderer (OpenGL)", "done"),
    # No libnx or deko3d code exists: src/platform/switch/ is not created yet.
    ("Switch renderer (deko3d)", "todo"),
    # Mixer, SDL output and the floor-1 decoder work and the mixer has unit
    # tests. Partial because the Ogg decoder has no unit test, its floor-0
    # path dlopens the host libvorbisfile (absent on the Switch) and the
    # sound/strmfile event table is not wired to gameplay.
    ("Audio", "partial"),
    # Keyboard and Switch button maps, deadzone and SDL game-controller
    # support (src/platform/input.cpp, tests/unit/test_platform.cpp).
    ("Input", "done"),
    # Inventory only (docs/FORMATS/video.md); no Bink decoder or playback.
    ("Video (Bink)", "todo"),
]
