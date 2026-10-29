# Region entity table

Status: **entity names, models, positions and properties decoded**. The
`region` entry of each level `.big` is the placement table for scripted
entities and fixed props. Implementations: `tools/bo2fmt/region.py` and
`src/engine/region.cpp` (the host parses the table and populates its
entity world).

## Container

The `region` payload is itself a `goefile` container. Its directory uses
simple entries instead of the usual symbol-list entry:

```
char[8] type
u32     total size, including this 12-byte header
        payload
```

The payload:

```
u32 version
u32 extra
repeated section:
  char[8] name
  u32     total size, including this 12-byte header
  payload
```

The observed section names are `packages`, `entities`, `cellgrup` and
`demodata`. An `entities` section payload starts with two u32 values
(package and marker) followed by `entity` chunks:

```
char[8] "entity"
u32     total size, including this 12-byte header
u32     flags
cstr    entity name
cstr    model name
        pad to 4
Vec3    position
repeated property:
  cstr  key, padded to 4
  u32   type
  value (type 3/2: u32; type 8: cstr)
```

## Measured

Across the installed tree `region.py` extracted **15,632 entities** with
**533 distinct model names** and no parse errors. The sanctuary region
places `dagger`, `kainemitter`, `ptorch`, `l_club`, `lsword` and
`regionkeeper`; the model names are short names resolved through the
package/resource tables.

## Host

`parse_region_entities` unwraps the nested container and returns the
entity tables; `bo2nx --level` uses the region entities for its
visibility/culling diagnostics. This is the first real entity population
path for M3. The entity type (the `model` field above) names an entity
template, `assets/etypes/**/<type>.etp`, in the same container; types whose
template has `flag helperobj` are not drawn (see "Helper objects" in
[`models.md`](models.md)).

## Tools

```sh
python3 tools/bo2fmt/region.py list level.big
python3 tools/bo2fmt/region.py entities level.big --out entities.json
```

## Open questions

- The `packages` section and the short-name-to-`model` resource mapping.
- The remaining property types.
- `cellgrup`/`demodata` sections.
- Shared props such as `pilara`/`curtaina` do not appear in `region` or
  `cellinst`; their loader is still unknown (tracked as #66).
