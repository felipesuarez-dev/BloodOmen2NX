# src/platform - the only layer that touches the OS

Backends share one interface:

- `headless/` - software rasterizer for development containers without a
  display. It decodes a real texture and writes a PPM frame; it is the
  current first-frame backend.
- `sdl/` - SDL2 + OpenGL host backend, built when the development
  libraries are present. It opens a window, renders the level with a free
  camera and can capture a frame under Xvfb.
- `switch/` - libnx + deko3d, the release target (`.nro`).

The host backend exists so the game and engine layers can be built,
tested and compared against the original without a console. The Switch
backend is a port of the platform layer, not of the game.

Status: the headless and SDL backends render the sanctuary level with
textures and a free camera; the next step is the oracle frame comparison.
