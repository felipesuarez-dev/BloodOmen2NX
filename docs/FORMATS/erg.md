# `game.erg`

Status: **verified** (text configuration).

## Where it comes from

The file is not on the ISO. It lives in the installer's SFXI container
(`install2.dat` -> `FILESYS/107` -> `game.erg`, 754 compressed bytes,
1,708 uncompressed) and the installer script copies it to `data/`.

## Shape

A plain-text, line-oriented configuration file. It is not a binary
container and not the level data. It contains:

- section-style keys with quoted paths, for example feature flags,
  debug switches and language settings;
- runtime paths: `basePath`, `ArchiveFile`, `Level`, movie paths;
- numeric tuning values: render heap size, scene display-list size, and
  similar.

Two keys matter for the port:

```
ArchiveFile="...pc.rkv"
usebigfile=1
```

They confirm that the asset payload lives in `pc.rkv`, not in the
numbered `.dat` files. The installer expands `pc.rkv` from one of those
chunks.

## Open questions

- Which keys the engine reads at boot and which are debug-only.
- Whether the file is rewritten by the game (settings) or read-only.
- The exact parser (section names, quoting, `-` prefixed debug keys).

## Next steps

1. Find the reader in `bo2.exe` and decompile the key lookup.
2. Port the parser as a small, tested module.
3. Keep the sample file local; only the schema and key names go in this
   repository.
