# Native Windows oracle

This guide is for the development host (`ubuntu-m75q`,
`/home/felipe/DECOMPILACION/BloodOmen2NX`). The native Windows oracle runs the
original PC build on a Windows host, without Wine, and serves a capture
directory to the development host through an SSH reverse tunnel. The Wine
path and its blocker are documented in `docs/ORACLE_COMPARE.md`.

## TL;DR

The Windows host publishes the harness on the development host:

```
ubuntu 127.0.0.1:8765  <--SSH--  Windows 127.0.0.1:8765  (oracle_server.py)
```

From the repository:

```sh
python3 tools/oracle/oracle_client.py health
python3 tools/oracle/oracle_client.py list
python3 tools/oracle/oracle_client.py summary capture_YYYYMMDD_HHMM
python3 tools/oracle/oracle_client.py download capture_YYYYMMDD_HHMM \
    --dest external/captures
python3 tools/oracle/validate_capture.py external/captures/capture_YYYYMMDD_HHMM
python3 tools/oracle/align.py external/captures/capture_YYYYMMDD_HHMM
```

The token is local to the development host and is never committed. The client
reads it from `$ORACLE_TOKEN`, `$ORACLE_TOKEN_FILE`,
`~/.config/bo2oracle/token`, `./.oracle_token`, `tools/oracle/.oracle_token`
or the script directory. The repository copy is ignored by `.gitignore`.

## Topology

| Node | LAN | Tailscale | User |
|---|---|---|---|
| Windows (oracle) | `192.168.1.16` | `100.121.86.23` | `felip` |
| Ubuntu (decomp) | `192.168.1.2` | `100.105.21.49` | `felipe` |

- The tunnel starts on Windows: `ssh -N -R 8765:127.0.0.1:8765 felipe@192.168.1.2`.
- The development host only needs `http://127.0.0.1:8765`; no Windows firewall
  port is opened.
- On Windows the harness lives in `C:\Users\felip\Documents\BO2Oracle` and
  listens on loopback only.
- If the tunnel drops, relaunch `scripts\start_oracle_harness.ps1` on Windows.
  The watchdog retries every 30 seconds.

## Client

`tools/oracle/oracle_client.py` uses only the Python standard library.

```sh
python3 tools/oracle/oracle_client.py health
python3 tools/oracle/oracle_client.py list
python3 tools/oracle/oracle_client.py summary <capture_id>
python3 tools/oracle/oracle_client.py files <capture_id>

python3 tools/oracle/oracle_client.py download <capture_id> --dest external/captures
python3 tools/oracle/oracle_client.py download <capture_id> --no-camera --dest external/captures
python3 tools/oracle/oracle_client.py download <capture_id> --all --dest external/captures
python3 tools/oracle/oracle_client.py download <capture_id> --only inputs.jsonl screenshots/

python3 tools/oracle/oracle_client.py get <capture_id> screenshots/01_spawn.png --out /tmp/01_spawn.png
python3 tools/oracle/oracle_client.py run probe
python3 tools/oracle/oracle_client.py run capture --params '{"route":"base"}'
python3 tools/oracle/oracle_client.py job <job_id> --log
```

`camera_d3d8.jsonl` is large (tens of megabytes for a few minutes). Use
`--no-camera` when only the manifest, inputs or screenshots are needed. The
client normalizes capture paths and rejects absolute or traversing names
before writing.

## HTTP API

All `/api/*` and `/files/*` routes require:

```http
Authorization: Bearer <token>
```

| Method | Path | Purpose |
|---|---|---|
| GET | `/health` | Status, timestamp and capture names |
| GET | `/api/captures` | Capture list with flags and a manifest summary |
| GET | `/api/captures/<id>` | Manifest, notes, file list and state preview |
| GET | `/api/captures/<id>/file?path=...` | One file inside the capture |
| GET | `/files/<id>/<path>` | Download route; supports `Range` |
| GET | `/api/jobs` | Recent jobs |
| POST | `/api/jobs` | `{"type":"probe"}` or `{"type":"capture","params":{...}}` |
| GET | `/api/jobs/<id>` | Job status and log tail |
| POST | `/api/jobs/<id>/cancel` | Cancel a running job |

## Capture layout

```
capture_YYYYMMDD_HHMM/
  game_manifest.json     hashes, version, resolution, configuration, tools
  notes.md               exact steps, commands, divergences and blockers
  SHA256SUMS             hashes of the capture artifacts
  inputs.jsonl           one input event per line
  state.csv              per-frame camera; player fields only when verified
  camera_d3d8.jsonl      raw D3D8 events: present, VIEW, PROJECTION, viewport
  video.mkv              60 fps capture of the 800x600 window, when available
  screenshots/           PNG at key points
  frames_ppm/            key frames ready for tools/compare_frames.py
  saves/                 save files written by the game
  memory/                dumps or deltas when offsets were not found
  logs/                  Frida, ffmpeg, installer and server logs
```

## Formats

`inputs.jsonl` has one event per line:

- `epoch_ms`: wall clock in milliseconds since the epoch.
- `mono_ns`: monotonic clock.
- `event`: `down`, `up`, `mouse_down`, `mouse_up`, `mouse_move` or `phase`.
- `key`: logical name (`w`, `s`, `a`, `d`, `shift`, `space`, `rmouse`, `lmouse`).
- `duration_ms`: present on the matching `up`.
- `phase`: `menu`, `spawn_idle`, `after_w`, `jump`, `attack`, `camera`, ...
- `dx`, `dy`: mouse movement.

`state.csv` columns: `frame`, `epoch_ms`, `mono_ms`, `fps_inst`, `cam_x`,
`cam_y`, `cam_z`, `cam_yaw_deg`, `cam_pitch_deg`, and player fields only when
they were verified (`player_x/y/z`, `vel_x/y/z`, `grounded`, `health`, `mana`,
`weapon`, `anim`, `tick`). Missing player fields stay empty and `notes.md`
must explain why.

The camera pose is derived from `SetTransform(D3DTS_VIEW, ...)`. The
projection matrix is stored in `camera_d3d8.jsonl`. Do not assume 25 Hz or
60 Hz: measure the present and logic rates from the capture.

## Validation workflow

1. Download a capture, or only the needed artifacts.
2. Run `validate_capture.py`. It checks the required files, the `bo2.exe`
   hash, `SHA256SUMS`, the resolution, the state columns and the counts.
   Use `--partial` for an incomplete capture.
3. Run `align.py`. It maps every input event to the nearest captured frame,
   summarizes the present rate and lists the recorded phases.
4. Compare frames with `tools/compare_frames.py`:

   ```sh
   python3 tools/compare_frames.py port.ppm \
       external/captures/<capture_id>/frames_ppm/01_spawn.ppm \
       --out /tmp/gate --tiles 4 --diff-gain 4
   ```

5. Compare the camera pose from the VIEW matrix and, when present, the player
   state. Record the frame, the associated input and the capture hash for
   every divergence.

## Current status (2026-10-28)

The tunnel, server, client and watchdog work. Four captures are visible:
`capture_20261027_2107`, `capture_20261027_2355`,
`capture_20261027_2358` and `capture_20261028_0047`. All are partial:

- `_2107` contains installer, menu, calibration and cutscene images plus the
  original `game.erg` and `settings.cfg`. It has no video, inputs or camera.
- `_2355` and `_2358` contain video, inputs and camera data. They have no
  `game_manifest.json`, `notes.md`, `SHA256SUMS`, `state.csv`,
  `frames_ppm/` or `saves/`. The `has_memory` flag is set but no memory files
  are listed.
- The first two gameplay attempts aborted with
  `RuntimeError: gameplay not detected`. The runner never left the intro
  cutscene. In `_2355` the runner tried `Space`; image comparison shows
  `Space` has no effect (MAE 0.12), while `Escape` changes the frame
  (MAE 19.65).
- `_0047` reached gameplay. The key frames show the first-level spawn,
  movement, the checkpoint, and the pause and save menus; the save header is
  `slums/sl01.hidden_checkpoint001`. It still has no manifest, notes, hashes,
  `state.csv` or `frames_ppm/`, and the camera hook stops 11.6 s before the
  spawn phase, so 67 of 75 inputs are outside the camera coverage. See
  `docs/ORACLE_WINDOWS.md` for the derived facts.
- The HTTP job API runs `probe` jobs, but the last `capture` job exited with
  `ModuleNotFoundError: No module named 'frida'`. The server's Python
  environment is not the one used for the manual captures.
- `present` events in `_2358` run at roughly 200 to 300 fps in menus and
  cutscenes. There is no effective vsync; the logic tick must be measured in
  gameplay.
- `_2355` reports a 1920x1080 client while ffmpeg recorded an 800x600 region.
  `_2358` uses an 800x600 client. The final capture must keep the client and
  the video region at 800x600.
- The recommended final run starts from a manual save created at the first
  level. That avoids the cutscene detector entirely. If a save cannot be
  used, the runner must skip with `Escape` and wait for a gameplay frame
  before starting the route.

## Final capture checklist

- `game_manifest.json` with the reference `bo2.exe` SHA-256
  `91758398a17de9c9f974a8a102762a534fb523d40fa6868a5e5a4f0483b2753c`,
  version, 800x600, windowed mode and the installed tools.
- `notes.md` with the exact route, keys, save used, blockers and measured
  present/tick rates.
- `SHA256SUMS` for every artifact.
- `inputs.jsonl` with wall and monotonic timestamps and phase markers.
- `state.csv` with camera fields always and player fields when verified.
- `camera_d3d8.jsonl` with `present`, VIEW and PROJECTION events.
- `video.mkv` at 800x600 and `video_start.json` with the first frame.
- `screenshots/` and `frames_ppm/` at spawn, after W, after the jump, after
  the attack and after the camera rotation.
- `saves/` with the save used to enter the level.

The route is: idle 5 s, W 2 s, S 1 s, A 1 s, D 1 s, Shift+W 2 s, jump with
the right mouse button and wait 1 s, attack with the left mouse button and
wait 1 s, rotate the camera and wait 2 s, then save if the game allows it.
The original controls are right mouse for jump, left mouse or `Space` for
action/attack, WASD to move, mouse for the camera and `C` to block.

## Security and limits

- The server listens on loopback only and is published through the SSH
  tunnel; it is not exposed to the Internet.
- The token stays on the local machines. `tools/oracle/.oracle_token` is
  ignored by `.gitignore`; `~/.config/bo2oracle/token` is preferred.
- `external/captures/` is ignored through the `external/` rule. Never commit
  game assets, video, frames, saves, memory dumps or screenshots with
  original content.
- The client and the validators are read-only tools except for an explicit
  `download` or `get`.

## Troubleshooting

- `curl: (7) Failed to connect`: the tunnel is down or the server is not
  running on Windows. Check `logs\oracle_tunnel.err.log` and
  `logs\oracle_server.err.log` on Windows.
- `HTTP 401`: wrong or missing token. Use the client or set
  `ORACLE_TOKEN_FILE=$PWD/tools/oracle/.oracle_token`.
- `HTTP 404` for a capture: the capture has not finished or the name is
  wrong; `list` shows what is available.
- Port `8765` already in use on Ubuntu: start the Windows tunnel on another
  remote port and set `ORACLE_URL=http://127.0.0.1:<port>`.
- `run capture` exits with `ModuleNotFoundError: No module named 'frida'`:
  launch the job with the same Python or virtual environment that has Frida
  installed, or install Frida in the server's Python. `run probe` works
  because it does not import Frida.
- Downloads can resume because the server supports `Range`; the client
  currently restarts the file.

## Next steps

1. Produce a final capture from a manual save at the first level.
2. Verify the `bo2.exe` hash and the capture hashes.
3. Align the inputs to frames and measure the gameplay tick.
4. Compare a short window with `tools/compare_frames.py` and the camera
   state.
5. Record the divergences and update `docs/ORACLE_WINDOWS.md`, Plane and the
   milestone docs. Do not close the playable-slice gate without state parity.
