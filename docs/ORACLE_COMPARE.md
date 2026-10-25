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

Not started. The oracle is optional and time-boxed; see
`docs/RUNNING.md`.

## Rules

- A screenshot comparison is a gate, not a demo. Do not show a level until
  the frame recognizably matches the original.
- Keep oracle captures under `external/captures/` (gitignored). Only derived
  facts (hashes, dimensions, counts) are committed.
- Record every divergence with a `FUN_` reference and a next action.
