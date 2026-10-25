# Oracle comparison

The oracle is the original PC build running under Wine. It is used to prove
behaviour, not to run the game for fun.

## Method

1. Capture a reference moment from the original: screenshot, state dump,
   audio sample or save file.
2. Reproduce the same moment in the port with scripted input and a fixed
   tick.
3. Compare. For visuals, put both frames side by side; for state, compare
   canonical hashes; for saves, compare the byte layout.

## Status

**Launches, frame capture pending.** On 2026-10-25 the Spanish GoldMaster
launches under Wine 9.0 on Xvfb: the initial warning dialog appears and
the D3D8 device is created (vertex buffers are locked). After accepting
the dialog the process faults at `0x56e2c9` in `FUN_0056e2b0` with a null
`param_1[2]` (`read access to 0x1c`). The install tree was mounted as
`C:\gogames\bo2` in a private prefix, with `bo2.exe`, `data/` and the
`code.big`/`pc.rkv`/`resources` files at the root. The next step is to
inspect the display/init path behind `FUN_0056e2b0` and the GOG registry
or launcher requirements. The oracle remains optional and time-boxed; see
`docs/RUNNING.md`.

## Rules

- A screenshot comparison is a gate, not a demo. Do not show a level until
  the frame recognizably matches the original.
- Keep oracle captures under `external/captures/` (gitignored). Only derived
  facts (hashes, dimensions, counts) are committed.
- Record every divergence with a `FUN_` reference and a next action.
