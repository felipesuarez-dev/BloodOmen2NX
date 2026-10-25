# src/platform - the only layer that touches the OS

Backends share one interface:

- `headless/` - software rasterizer for development containers without a
  display. It decodes a real texture and writes a PPM frame; it is the
  current first-frame backend.
- `linux/` - SDL2 + OpenGL, the target host backend. Pending: the
  development container does not provide SDL2 or OpenGL development
  libraries.
- `switch/` - libnx + deko3d, the release target (`.nro`).

The host backend exists so the game and engine layers can be built,
tested and compared against the original without a console. The Switch
backend is a port of the platform layer, not of the game.

Status: the headless backend renders a textured cube with a free camera;
the level renderer, input and window backend are next.
