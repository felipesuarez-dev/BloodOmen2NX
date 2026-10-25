# Installer chunks (`data/*.dat`)

Status: **decoded and validated**. Implementation:
`tools/bo2fmt/installer.py` (pipeline) and `tools/bo2fmt/bo2cipher.c`
(block-cipher accelerator). The 198 chunks on the reference ISO expand to
the installed tree; the pipeline produced 227 files (1.9 GB) and every
rebuilt archive passed `unrar`'s CRC check.

## Role

The numbered files are not the final asset containers and they are not a
single compressed blob. Each one is a **single-file RAR 4 archive** with
two layers:

1. a repeating XOR over the whole file, keyed by the stored size, the
   decimal chunk name and the lowercased target name;
2. the installer's own 32-round block cipher over the RAR packed data.

The installer script (`install2.dat` -> SFXI -> `script.txt`) maps 198
chunks to target paths and copies 29 other files unchanged:

| Install type | Files | Targets |
|---|---:|---|
| `decompress` | 198 | 179 `.big`, 18 `.san`, 1 `pc.rkv` |
| `copy` | 29 | `bo2.exe`, `binkw32.dll`, `game.erg`, `list.txt`, UI raw images, fonts, Bink movies |

`pc.rkv` is the audio archive; the level, model, texture and entity data
lives in the `.big` files.

## Chunk selection

The executor (`install2.dat` at `0x401b30`) builds `data\%u.dat` from a
32-bit hash of the target path and name. The decimal `.dat` file name is
that hash: for example the `pc.rkv` chunk is `538893502.dat` and the
`PreCode.big` chunk is `1635697181.dat`.

The extractor does not need to reproduce the hash. It reads the hash from
the file name and uses it in the XOR key. After deobfuscation, the RAR
entry name is the target file name, compared case-insensitively. The
target path comes from `script.txt`.

## XOR layer

```
size        = u32 at chunk offset 0
hash        = decimal value of the .dat file name
name        = lowercased target file name
key         = le32(size ^ 0x21726152) || le32(hash) || (~name bytes)
plain[i]    = chunk[i] ^ key[i % len(key)]
```

`0x21726152` is `"Rar!"` read as a little-endian word, so the first four
plaintext bytes are the RAR signature. `~name` means `0xff - byte`. The
name is lowercased by the installer; this matters for targets such as
`PreCode.big` (the RAR entry keeps its case, the key uses
`precode.big`).

The XOR is applied to the whole file. The rest of the file is a normal
RAR 4 archive; see `docs/FORMATS/rkv.md` for the audio archive and
`docs/FORMATS/big.md` for the content containers.

## RAR container

Measured on all 198 chunks:

```
signature       "Rar!\x1a\x07\x00"          (7 bytes)
main header     type 0x73, size 13, flags 0x0008
file header     type 0x74, flags 0x8084
  flag 0x0004   password / custom cipher
  flag 0x8000   data follows the header
  method        0x35 (RAR 2.9/3.x)
  unpacked ver  20
  header size   32 + name length (no salt, no extended time)
  one file      header + packed data = end of chunk
```

The chunk is a single-file archive. The custom cipher is used only when
`flags & 0x0004` and unpacked version is 20; that is the case for all 198
chunks.

## Custom cipher

The setup reads a 37-byte password and a 256-byte S-box from the user's
`install2.dat`; neither is stored in this repository. For the reference
Spanish GoldMaster:

| Item | Location in `install2.dat` |
|---|---|
| S-box (256 bytes) | file offset `0x27410` |
| Password (NUL-terminated) | file offset `0x261a4` |

The cipher works on 16-byte blocks (four little-endian u32 words):

- A fixed 128-bit key seed is loaded.
- The S-box is shuffled 256 times using the password bytes and the
  standard reflected CRC-32 table (polynomial `0xedb88320`). The low byte
  of each table entry selects the swap; the password is read in steps of
  two and byte `i+1` is the stop value for byte `i`.
- The password buffer (256 bytes, NUL-padded) is encrypted block by block
  to advance the key. For the 37-byte password, blocks at offsets 0, 16
  and 32 are processed.
- Each 32-round block function uses two 32-bit rotates (11 and 15 bits),
  the four key words, a byte-wise S-box substitution and the two chaining
  words. The forward function is at `0x405110`; the inverse at `0x405300`.
- After each block, the key is XORed with CRC-table entries selected by
  the 16 bytes of the **ciphertext** block (`0x405280`). Encrypting the
  password and decrypting the data therefore advance the same key state.

The setup is at `0x405490`, the password checksum helper at `0x405610`
and the CRC-table generator at `0x4050a0`. The XOR read wrapper is
`0x404f60`; the header parser is `0x405b90` / `0x4059f0`; the extractor
loop is `0x405810` and `0x4069a0`.

## Extraction

`tools/bo2fmt/installer.py`:

1. Parses `script.txt`.
2. Maps the 198 target names to chunks by trying each lowercased name
   against the first RAR header (198 names, one match each).
3. Deobfuscates the chunk, parses the RAR header, decrypts the packed
   data, clears the password flag and rebuilds a standard RAR.
4. Streams `unrar p` into the target path and checks the unpacked size.

`bo2cipher.c` is compiled on demand for the XOR and block-cipher hot
loops; the Python `Cipher` class is the reference and the fallback. The
tests compare the two implementations.

## Verified result

| Metric | Value |
|---|---:|
| Chunks mapped | 198 / 198 |
| Installed files | 227 |
| Installed size | 1,968,053,271 bytes |
| `.big` files | 179 |
| `.big` entries parsed | 246,332 |
| DDS textures | 22,483 |
| Models | 5,168 |
| `pc.rkv` streams | 2,666 |

The installed tree's manifest hash is in `docs/PROVENANCE.md`. The
first `pc.rkv` stream is Ogg Vorbis mono 44.1 kHz. The last stream is
850 bytes shorter than its declared size; see `docs/FORMATS/rkv.md`.

## Open questions

- The exact chunk-hash function at `0x401320` (not needed for extraction;
  the hash is read from the file name).
- The installer's internal RAR decoder is not ported; `unrar` is used for
  development extraction.
- Whether other builds use a different S-box, password or key seed.

## Legal

The repository contains no game assets, no S-box bytes and no password.
The tool reads the S-box and password from the user's own `install2.dat`.
The extracted tree lives under `external/`, which is gitignored.
