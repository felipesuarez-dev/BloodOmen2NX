"""Progress inputs for tools/gen/progress.py.

LEVELS: the 11 campaign chapters in game order, as (name, completable). A
        chapter is completable when the port plays it from its entry region to
        the hand-off into the next chapter (the end, for the last one). The
        list, its source in bo2.exe and the region mapping are in
        docs/LEVELS.md; tests/python/test_progress.py keeps both in step.

PLATFORM_FEATURES: (name, state) with state in {"done", "partial", "todo"}.
"""

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
