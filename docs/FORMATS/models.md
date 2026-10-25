# Models and animation

Status: **payload extracted; layout unknown**.

The engine lineage is the one used by *Mad Dash Racing* (Crystal Dynamics).
Models live inside `.big` files as `model` chunks; the taxonomy also
includes `cellgrup`, `region`, `planinfo`, `animatio` and `animreel`.
The installed tree has 5,168 `model` entries across 179 `.big` files.

## Observed payload

A model payload starts with a length, a `default` name and a size,
followed by a chunk tag. The first chunk of a sample from
`sanctuary.big` is `rmd`, followed by named records (`default`,
`position`, ...). The same `rmd` tag appears in the `bpackage` entries.

Public experimental converters describe separate vertex, normal, UV and
index streams, a per-face texture index list, submeshes, and skinning
weights per vertex. Those are hints, not a spec; the converters report
known failures on some files.

## Open questions

- Mesh layout: vertex format, index width, primitive type, bone weights.
- The `rmd` chunk and its named records.
- Skeleton and animation: node hierarchy, keyframe encoding, clip table.
- Material binding: texture page, UV set, blend mode.
- Where the level instance table lives (`planinfo`, `cellgrup`, `region`
  or the `bpackage` payload).

## Next steps

1. Find the model loader in `bo2.exe` and decompile the header read.
2. Build a synthetic fixture from the decoded header before parsing a
   real model.
3. Render one static mesh on the host backend; then one animated mesh.
4. Document `cellgrup`, `region` and `planinfo` for the level loader.
