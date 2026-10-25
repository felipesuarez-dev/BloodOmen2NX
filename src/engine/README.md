# src/engine - Blood Omen 2 engine reimplementation

The original engine (the Mad Dash Racing lineage used by Crystal Dynamics)
is statically linked into `bo2.exe`; there is no engine DLL to mirror.
This layer recovers the engine services behind stable interfaces:

- file access and container readers (`game.erg`, `.big`, `.dat`)
- texture and model codecs
- scene graph and camera
- audio buffers and mixer
- math types

The game layer depends on these interfaces only. No SDL, OpenGL, libnx or
deko3d include is allowed here.

Status: initial modules.

| Module | Responsibility | State |
|---|---|---|
| `big_archive` | `goefile` container reader | Validated on the installed tree |
| `cell_group` | `cellgrup` static level geometry and textures | Validated on `sanctuary` |
| `dds` | DDS/BC texture decoder | DXT1, DXT3, DXT5, A8R8G8B8; top mip |
| `math` | Vec3/Vec4/Mat4 | Used by the headless renderer |

The scene, model, audio and level interfaces land with the level
pipeline. The Python reference implementations remain the specification
in `tools/bo2fmt/`.
