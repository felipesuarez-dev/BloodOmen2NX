# `.dat` installer chunk

Status: **observed**, payload not decoded.

## Role

These are not the final asset containers. The installer script
(`install2.dat` -> SFXI -> `script.txt`) marks most of them as
`InstallType = decompress` and gives each one a target path such as:

```
%install_path%\media\gogames\bo2\pc.rkv
%install_path%\media\gogames\bo2\code.big
%install_path%\media\gogames\bo2\resources\levels\...\*.big
%install_path%\media\gogames\bo2\resources\animations\...\*.san
```

The numbered `data/<n>.dat` files on the ISO are the compressed payloads;
the installer expands them to the real tree. `pc.rkv` is the main archive
declared by `game.erg` (`ArchiveFile`). `data/list.txt` (copied as-is)
holds the asset path list used to build or address that archive.

The installer also copies the Bink movies (`eidos.dat`, `crystal.dat`,
`bo2intro.dat`, `awakens.dat`, `outro.dat`, `spooky.dat`, `sr2_trailer.dat`,
`credits.dat`) directly into `data/` without renaming.

## Observed

The 198 files under `data/` on the reference ISO share a simple prefix: a
32-bit little-endian value, followed by high-entropy data. Three samples:

| File | File size | First `u32` | Difference |
|---|---:|---:|---:|
| `data/1084068013.dat` | 1,923,569 | 1,922,545 | 1,024 |
| `data/1109058233.dat` | 611,318 | 611,318 | 0 |
| `data/1171077693.dat` | 4,706,591 | 4,706,591 | 0 |

The value behaves like a payload length; the 1,024-byte difference in the
first sample suggests alignment or a header block. This is a lead, not a
conclusion.

A string scan of an 8 MB sample found no `OggS`, `BIK`, model, texture or
directory markers. The payload is compressed, encrypted, or both.

## Open questions

- Is the prefix a length, a checksum input, or both?
- Is the payload compressed with an engine codec, encrypted, or a
  self-contained archive with an internal index?
- Do the installed files keep the `.dat` extension, or does the installer
  rename them to `.big` / `.bgx`?
- Where is the filename table? The file names are numeric, so either the
  game resolves them by hash or the index is inside each container.

## Next steps

1. Extract `setup.exe` and `install2.dat` statically and compare the
   installed layout with the ISO files.
2. Find the reader in `bo2.exe` (strings `data\`, `game.erg`, open/read
   calls) and decompile it.
3. Write a parser skeleton with a synthetic fixture before touching a real
   asset.
4. Validate by re-packing one file and confirming the original accepts it.
