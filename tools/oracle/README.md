# tools/oracle

Harness for running the original game under Wine and comparing it with the
port. It is optional and time-boxed; the build never depends on it.

Planned pieces:

| Script | Purpose |
|---|---|
| `run_oracle.sh` | Launch the original with a private prefix |
| `capture.sh` | Xvfb + scripted input + screenshot/audio capture |
| `compare_frame.sh` | Side-by-side frame comparison |
| `drive.py` | Deterministic input script |

## Rules

- Captures live under `external/captures/` (gitignored).
- Only derived facts (hashes, dimensions, counts) are committed.
- Do not spend more than one day making the oracle boot in M0.
- A frame comparison is a gate, not a demo.

## Status

Empty until the oracle work starts.
