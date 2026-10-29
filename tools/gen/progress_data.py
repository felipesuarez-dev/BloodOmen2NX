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
    ("Host renderer (OpenGL)", "todo"),
    ("Switch renderer (deko3d)", "todo"),
    ("Audio", "todo"),
    ("Input", "todo"),
    ("Video (Bink)", "todo"),
]
