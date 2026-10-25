# Audio

Status: **`pc.rkv` decoded; event table unknown**.

## Observed

- `pc.rkv` is the PC audio archive: 2,666 length-prefixed entries under
  `vo/`, `streams/wav/` and `music/wav/`. The first stream decodes as
  Ogg Vorbis mono 44.1 kHz.
- The last entry declares 50,002 bytes and stores 49,152; the reference
  file ends there. The parser reports it as truncated.
- The PS2 version uses Sony ADPCM; GameCube uses DSP. Both are covered by
  vgmstream, but they are out of scope for the PC port.

## Open questions

- The 2,666 streams versus the 2,824 reported for the public GOG copy.
- Sample rate, channels and loop metadata per stream.
- Music: streamed Ogg or a separate module format.
- Event table: which entity or script triggers each sound. The `sound`
  and `strmfile` entries live in the `.big` files.

## Next steps

1. Document the `sound`/`strmfile` event table.
2. Decode more streams and compare them with the original's output.
3. Feed `vo/`, `streams/wav/` and `music/wav/` into the host audio
   backend.
