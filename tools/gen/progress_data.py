"""Progress inputs for tools/gen/progress.py.

LEVELS: one entry per playable level, in game order, as (name, completable).
        Empty until the level table is recovered from the original.

PLATFORM_FEATURES: (name, state) with state in {"done", "partial", "todo"}.
"""

LEVELS = []

PLATFORM_FEATURES = [
    ("Host build skeleton", "done"),
    ("Installer container (SFXI)", "done"),
    ("Asset containers", "todo"),
    ("Host renderer (OpenGL)", "todo"),
    ("Switch renderer (deko3d)", "todo"),
    ("Audio", "todo"),
    ("Input", "todo"),
    ("Video (Bink)", "todo"),
]
