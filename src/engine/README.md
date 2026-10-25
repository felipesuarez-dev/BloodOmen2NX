# src/engine - Blood Omen 2 engine reimplementation

The original engine (the Mad Dash Racing lineage used by Crystal Dynamics)
is statically linked into `bo2.exe`; there is no engine DLL to mirror.
This layer recovers the engine services behind stable interfaces:

- file access and container readers (`game.erg`, `.dat`)
- texture and model codecs
- scene graph and camera
- audio buffers and mixer
- math types

The game layer depends on these interfaces only. No SDL, OpenGL, libnx or
deko3d include is allowed here.

Status: empty until M1/M2.
