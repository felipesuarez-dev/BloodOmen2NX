# Audio

Status: **observed**, not decoded.

## Observed

- The PC version uses Ogg Vorbis for sound effects, dialogue and music.
  A public MIT parser and inventory confirm it: `pc.rkv` holds 2,824 Ogg
  streams under `vo/`, `streams/wav/` and `music/wav/`.
- The PS2 version uses Sony ADPCM; GameCube uses DSP. Both are covered by
  vgmstream, but they are out of scope for the PC port.
- No `OggS` marker was found in the sampled `.dat` payloads on the
  reference ISO because those chunks are encrypted installer containers;
  the audio lives in `pc.rkv`, which the installer expands from them.

## Open questions

- Container and index for the audio streams.
- Sample rate, channels and loop metadata.
- Music: streamed Ogg or a separate module format.
- Event table: which entity or script triggers each sound.

## Next steps

1. Decode the `.dat` container.
2. Search the decoded payloads for `OggS` and Vorbis headers.
3. Find the audio loader and the mixer setup in `bo2.exe`.
4. Decode one stream and compare it with the original's output.
