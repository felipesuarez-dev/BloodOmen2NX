# src/platform - the only layer that touches the OS

Two backends share one interface:

- `linux/` - SDL2 + OpenGL, used for development, tests and the oracle
  comparison. This is the first target.
- `switch/` - libnx + deko3d, the release target (`.nro`).

The host backend exists so the game and engine layers can be built,
tested and compared against the original without a console. The Switch
backend is a port of the platform layer, not of the game.

Status: empty until M2.
