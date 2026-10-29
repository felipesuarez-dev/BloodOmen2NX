# File formats

Status legend: **verified** = observed in the reference data and confirmed
by a parser or the original game; **observed** = seen but not decoded;
**unknown** = not investigated.

| Format | Status | Doc |
|---|---|---|
| SFXI installer container | verified | [`sfxi.md`](sfxi.md) |
| `.dat` installer chunk | verified: decoded and validated | [`dat.md`](dat.md) |
| `game.erg` | verified: parser and round-trip fixture | [`erg.md`](erg.md) |
| `.big` container (`goefile`) | verified: 179 files, 246,332 entries | [`big.md`](big.md) |
| `pc.rkv` audio archive | verified: 2,666 Ogg streams | [`rkv.md`](rkv.md) |
| `tunedata` (`kain`) | partially verified: container and movement fields | [`tunedata.md`](tunedata.md) |
| Models | payload extracted; layout unknown | [`models.md`](models.md) |
| Textures | verified: DDS payloads | [`textures.md`](textures.md) |
| Light maps (`litedata/*.slm`) | partially verified: 71 entries, palette + 4-bit indices | [`litedata.md`](litedata.md) |
| Audio | verified: Ogg Vorbis in `pc.rkv` | [`audio.md`](audio.md) |
| Video | verified (magic only) | [`video.md`](video.md) |
| Saves | unknown | - |

## Rules

- A format is documented only after its parser is validated against real
  data. Spec-level entries reconstructed from public parsers are marked
  as such and are not treated as decoded. Unverified ideas go under
  "Open questions", never in the spec.
- No raw asset content, original strings or copyrighted text in these
  documents. Sizes, offsets, counts and hashes are fine.
- Re-pack validation is part of "done": if the original game does not
  accept the result, the format is not decoded.
