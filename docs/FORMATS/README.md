# File formats

Status legend: **verified** = observed in the reference data and confirmed
by a parser or the original game; **observed** = seen but not decoded;
**unknown** = not investigated.

| Format | Status | Doc |
|---|---|---|
| SFXI installer container | verified | [`sfxi.md`](sfxi.md) |
| `.dat` installer chunk | encrypted, not decoded | [`dat.md`](dat.md) |
| `game.erg` | verified (text config) | [`erg.md`](erg.md) |
| `.big` container (`goefile`) | spec from public parsers | [`big.md`](big.md) |
| `pc.rkv` audio archive | spec from public parsers | [`rkv.md`](rkv.md) |
| Models | unknown payload | [`models.md`](models.md) |
| Textures | DDS payload observed | [`textures.md`](textures.md) |
| Audio | Ogg Vorbis (public parsers) | [`audio.md`](audio.md) |
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
