# Audio

Status: **observed**, not decoded.

## Observed

- Community documentation states that the PC version uses Ogg Vorbis for
  sound effects, dialogue and music, while the PS2 version uses Sony ADPCM
  and a tracker-style music format.
- No `OggS` marker was found in the sampled `.dat` payloads on the
  reference ISO, so the Ogg streams are either inside a decoded region,
  compressed, or not in those samples.

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
