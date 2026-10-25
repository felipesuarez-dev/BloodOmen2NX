# Models and animation

Status: **unknown**.

The engine lineage is the one used by *Mad Dash Racing* (Crystal Dynamics).
The original console and PC builds share the same content, but the PC
container layout is the one that matters here.

## Open questions

- Mesh layout: vertex format, index width, primitive type, bone weights.
- Skeleton and animation: node hierarchy, keyframe encoding, clip table.
- Material binding: texture page, UV set, blend mode.
- Where the model table lives (level container, `game.erg`, or a separate
  archive).

## Next steps

1. Decode the `.dat` container first; models are inside it.
2. Find the model loader in `bo2.exe` and decompile the header read.
3. Build a synthetic fixture from the decoded header before parsing a real
   model.
4. Render one static mesh on the host backend; then one animated mesh.
