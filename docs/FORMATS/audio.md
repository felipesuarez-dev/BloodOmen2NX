# Audio

Status: **`pc.rkv` decoded; `sound` metadata and `strmfile` scripts
extracted; the host decodes and mixes the Ogg streams; the meaning of the
`sound` property fields is open**.

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
- `sound` payload layout: NUL-terminated record name, u32 total size, two
  header u32 values, then 8-byte `(u16 key, u16 field, u32 value)` records.
  Some records leave a four-byte remainder. The keys and fields are not
  interpreted yet.

## Next steps

1. Document the `sound`/`strmfile` event table.
2. Decode more streams and compare them with the original's output.
3. Feed `vo/`, `streams/wav/` and `music/wav/` into the host audio
   backend. The host decoder and mixer are implemented; integration with
   the event table is pending.

## Extraction tools

```sh
python3 tools/bo2fmt/rkv.py extract pc.rkv --out audio
python3 tools/bo2fmt/audio.py sounds level.big --out sounds.json
python3 tools/bo2fmt/audio.py strmfiles level.big --out events
```

`sounds` records the entry name, version, flags, property records and any
remainder. `strmfiles` writes the entity/event scripts with their original
CRLF line endings.
