# src/platform - the only layer that touches the OS

Backends share one interface:

- `headless/` - software rasterizer for development containers without a
  display. It decodes a real texture and writes a PPM frame; it is the
  current first-frame backend.
- `sdl/` - SDL2 + OpenGL host backend, built when the development
  libraries are present. It opens a window, renders the level with a free
  camera and can capture a frame under Xvfb.
- `linux/` - host platform services: file system, log, monotonic clock and
  memory snapshot. It has no window and is covered by the platform tests.
- `switch/` - libnx + deko3d, the release target (`.nro`).

The host backend exists so the game and engine layers can be built,
tested and compared against the original without a console. The Switch
backend is a port of the platform layer, not of the game.

The shared interfaces live in `include/bo2/platform/platform.hpp`. The
Switch backend implements the same services.

Status: the host services, headless renderer and SDL2/OpenGL backend are
in place; the next step is the oracle frame comparison.
