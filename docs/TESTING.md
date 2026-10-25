# Testing strategy

## Pyramid

| Level | What | When | Assets needed |
|---|---|---|---|
| Unit | LE readers, math, parsers on synthetic fixtures, state machines | Always | No |
| Parity Python/C++ | Same input to the reference parser and the C++ parser -> identical output | Always | No (fixtures) |
| Integration (asset-gated) | Parsers over the user's real assets -> counts and SHA-256 manifests | `BO2NX_ASSETS_DIR` set | Yes, skips otherwise |
| Render golden | Offscreen render of a scene or texture -> PNG hash | `BO2NX_ASSETS_DIR` set | Yes, goldens local |
| Headless regression | `bo2nx --headless --frames N --dump-state` -> canonical hash | Always (synthetic) / gated (real levels) | Mixed |
| Oracle | Port vs original under Wine (screenshots, state, saves, audio) | Milestones | See `docs/RUNNING.md` |
| Sanitizers | ASan + UBSan over unit and integration | `linux-asan` preset | No |

## Rules

- Every feature or fix lands with its tests in the same commit.
- Asset-dependent tests skip cleanly when `BO2NX_ASSETS_DIR` is unset;
  they never fail for lack of assets.
- Committed goldens contain only derived facts (hashes, counts,
  dimensions). Raw asset content and copyrighted strings are never
  committed.
- Regenerate goldens only after an intentional behaviour change, and
  explain why in the commit message.
- No test may depend on a wall clock, a network or the user's home layout.

## Commands

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure

BO2NX_ASSETS_DIR=/path/to/game ctest --preset linux-debug   # asset-gated on
./scripts/ci.sh                                             # everything + policy check
```

## Visual gate

Once the host renderer exists, `tools/oracle/compare_frame.sh` will put a
port frame next to an oracle capture. It is a manual gate: look at the
side-by-side image before and after every geometry or camera change. Do
not show a demo until the frame recognizably matches the original.
