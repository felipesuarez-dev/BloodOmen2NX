#!/usr/bin/env python3
"""Materialize the Blood Omen 2 install tree from the reference ISO.

The installer stores most of the tree in ``data/<n>.dat`` chunks. Each
chunk is a RAR 4 archive obfuscated with a repeating XOR key and, for the
packed data, encrypted with the installer's own block cipher. This module
reproduces that pipeline:

1. ``script.txt`` (SFXI) names the targets and their install paths.
2. The XOR key is built from the stored size, the decimal hash in the
   ``.dat`` file name and the lowercased target name.
3. The RAR file header is parsed after deobfuscation. The packed data is
   decrypted with a key derived from the password and the 256-byte S-box.
4. A standard RAR is rebuilt with the password flag cleared and ``unrar``
   decompresses it.

The S-box and password are read from the user's ``install2.dat``; neither
is stored in this repository. Offsets are the Spanish GoldMaster build.
"""

from __future__ import annotations

import argparse
import ctypes
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

RAR_MAGIC = b"Rar!\x1a\x07\x00"
RAR_XOR = 0x21726152
SBOX_OFFSET = 0x27410
SBOX_SIZE = 256
PASSWORD_OFFSET = 0x261A4
KEY_SEED = (0xD3A3B879, 0x3F6D12F7, 0x7515A235, 0xA4E7F123)
MASK32 = 0xFFFFFFFF
FILE_HEADER = 0x74
MAIN_HEADER = 0x73
END_BLOCK = 0x7B
ENCRYPTED = 0x0004
DATA_FOLLOWS = 0x8000


class InstallerError(ValueError):
    pass


class RarError(InstallerError):
    pass


def _crc16(data: bytes | bytearray) -> int:
    return zlib.crc32(data) & 0xFFFF


def crc_table() -> list[int]:
    table = []
    for index in range(256):
        value = index
        for _ in range(8):
            value = (value >> 1) ^ 0xEDB88320 if value & 1 else value >> 1
        table.append(value)
    return table


def load_installer(
    path: Path,
    sbox_offset: int = SBOX_OFFSET,
    password_offset: int = PASSWORD_OFFSET,
) -> tuple[bytes, bytes]:
    data = path.read_bytes()
    if len(data) < sbox_offset + SBOX_SIZE or len(data) <= password_offset:
        raise InstallerError(f"{path}: too short for the installer tables")
    sbox = data[sbox_offset : sbox_offset + SBOX_SIZE]
    end = data.find(b"\0", password_offset)
    if end < 0:
        raise InstallerError(f"{path}: unterminated password")
    password = data[password_offset:end]
    if not password:
        raise InstallerError(f"{path}: empty password")
    if len(password) > 255:
        raise InstallerError(f"{path}: password too long")
    return sbox, password


def xor_key(size: int, hash_value: int, name: str | bytes) -> bytes:
    raw = name.encode("latin1") if isinstance(name, str) else name
    return struct.pack("<II", size ^ RAR_XOR, hash_value) + bytes(b ^ 0xFF for b in raw.lower())


def xor_bytes(data: bytes, key: bytes) -> bytes:
    if not data:
        return b""
    repeated = (key * (len(data) // len(key) + 1))[: len(data)]
    return (int.from_bytes(data, "little") ^ int.from_bytes(repeated, "little")).to_bytes(
        len(data), "little"
    )


def deobfuscate(data: bytes | bytearray, name: str, hash_value: int) -> bytes:
    size = struct.unpack_from("<I", data, 0)[0]
    key = xor_key(size, hash_value, name)
    return xor_bytes(bytes(data), key)


class Cipher:
    """Reference implementation of the installer block cipher."""

    def __init__(self, sbox: bytes, password: bytes, table: list[int] | None = None):
        if len(sbox) != SBOX_SIZE:
            raise InstallerError("the S-box must be 256 bytes")
        if not password:
            raise InstallerError("the password must not be empty")
        if len(password) > 255:
            raise InstallerError("the password must be at most 255 bytes")
        self._sbox_seed = bytes(sbox)
        self._password = bytes(password)
        self._table = table if table is not None else crc_table()
        self._table_bytes = [value & 0xFF for value in self._table]
        self.sbox = bytearray()
        self.key: list[int] = [0, 0, 0, 0]
        self.reset()

    def reset(self) -> None:
        self.sbox = bytearray(self._sbox_seed)
        self.key = list(KEY_SEED)

        password = self._password
        buffer = bytearray(256)
        buffer[: len(password)] = password
        length = len(password)
        for round_index in range(256):
            index = 0
            while index < length:
                stop = self._table_bytes[(buffer[index + 1] + round_index) & 0xFF]
                current = self._table_bytes[(buffer[index] - round_index) & 0xFF]
                step = 1
                while current != stop:
                    other = (current + index + step) & 0xFF
                    self.sbox[current], self.sbox[other] = self.sbox[other], self.sbox[current]
                    step += 1
                    current = (current + 1) & 0xFF
                index += 2

        for offset in range(0, length, 16):
            block = list(struct.unpack_from("<4I", buffer, offset))
            encrypted = self._encrypt_block(block)
            struct.pack_into("<4I", buffer, offset, *encrypted)

    def _sub_bytes(self, value: int) -> int:
        sbox = self.sbox
        return (
            (sbox[(value >> 24) & 0xFF] << 24)
            | (sbox[(value >> 16) & 0xFF] << 16)
            | (sbox[(value >> 8) & 0xFF] << 8)
            | sbox[value & 0xFF]
        )

    def _mix_key(self, block: bytes) -> None:
        key = self.key
        table = self._table
        for group in range(4):
            key[0] ^= table[block[4 * group]]
            key[1] ^= table[block[4 * group + 1]]
            key[2] ^= table[block[4 * group + 2]]
            key[3] ^= table[block[4 * group + 3]]

    def _encrypt_block(self, words: list[int]) -> list[int]:
        key = self.key
        u1 = words[2] ^ key[2]
        u2 = words[3] ^ key[3]
        left0 = words[0] ^ key[0]
        left1 = words[1] ^ key[1]
        u4 = u5 = 0
        for round_index in range(32):
            u5 = u2
            u4 = u1
            u1 = ((((u5 << 11) | (u5 >> 21)) & MASK32) + u4) ^ key[round_index & 3]
            u1 = self._sub_bytes(u1 & MASK32) ^ left0
            u2 = ((((u4 >> 15) | (u4 << 17)) & MASK32) ^ u5) + key[round_index & 3]
            u2 = self._sub_bytes(u2 & MASK32) ^ left1
            left0 = u4
            left1 = u5
        result = [key[0] ^ u1, key[1] ^ u2, key[2] ^ u4, key[3] ^ u5]
        self._mix_key(struct.pack("<4I", *result))
        return result

    def _decrypt_block(self, words: list[int]) -> list[int]:
        key = self.key
        original = list(words)
        u2 = words[0] ^ key[0]
        left1 = words[1] ^ key[1]
        u1 = words[2] ^ key[2]
        u3 = words[3] ^ key[3]
        u4 = u5 = 0
        for round_index in range(31, -1, -1):
            u5 = u3
            u4 = u1
            u1 = ((((u5 << 11) | (u5 >> 21)) & MASK32) + u4) ^ key[round_index & 3]
            u1 = self._sub_bytes(u1 & MASK32) ^ u2
            u2 = ((((u4 >> 15) | (u4 << 17)) & MASK32) ^ u5) + key[round_index & 3]
            u3 = self._sub_bytes(u2 & MASK32) ^ left1
            u2 = u4
            left1 = u5
        result = [key[0] ^ u1, key[1] ^ u3, key[2] ^ u4, key[3] ^ u5]
        self._mix_key(struct.pack("<4I", *original))
        return result

    def encrypt(self, data: bytes) -> bytes:
        output = bytearray()
        for offset in range(0, len(data), 16):
            chunk = data[offset : offset + 16]
            block = list(struct.unpack("<4I", chunk.ljust(16, b"\0")))
            output += struct.pack("<4I", *self._encrypt_block(block))
        return bytes(output[: len(data)])

    def decrypt(self, data: bytes) -> bytes:
        output = bytearray()
        for offset in range(0, len(data), 16):
            chunk = data[offset : offset + 16]
            block = list(struct.unpack("<4I", chunk.ljust(16, b"\0")))
            output += struct.pack("<4I", *self._decrypt_block(block))
        return bytes(output[: len(data)])


class CipherHelper:
    """Compiled C implementation of the XOR layer and the block cipher."""

    def __init__(self, library: Path):
        self._library = ctypes.CDLL(str(library))
        self._library.bo2_xor.argtypes = [
            ctypes.POINTER(ctypes.c_ubyte),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_ubyte),
            ctypes.c_size_t,
        ]
        self._library.bo2_xor.restype = None
        self._library.bo2_decrypt.argtypes = [
            ctypes.POINTER(ctypes.c_ubyte),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_ubyte),
            ctypes.POINTER(ctypes.c_ubyte),
            ctypes.c_size_t,
        ]
        self._library.bo2_decrypt.restype = None

    @staticmethod
    def _array(buffer: bytearray):
        return (ctypes.c_ubyte * len(buffer)).from_buffer(buffer)

    def xor_inplace(self, buffer: bytearray, key: bytes) -> None:
        if not buffer:
            return
        key_array = (ctypes.c_ubyte * len(key)).from_buffer_copy(key)
        self._library.bo2_xor(self._array(buffer), len(buffer), key_array, len(key))

    def decrypt_inplace(self, buffer: bytearray, sbox: bytes, password: bytes) -> None:
        if not buffer:
            return
        sbox_array = (ctypes.c_ubyte * SBOX_SIZE).from_buffer_copy(sbox)
        password_array = (ctypes.c_ubyte * len(password)).from_buffer_copy(password)
        self._library.bo2_decrypt(
            self._array(buffer),
            len(buffer),
            sbox_array,
            password_array,
            len(password),
        )


def compile_cipher_helper(source: Path, destination: Path) -> CipherHelper | None:
    compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
    if compiler is None:
        return None
    command = [compiler, "-O2", "-shared", "-fPIC", "-o", str(destination), str(source)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        return None
    return CipherHelper(destination)


class RarFile:
    __slots__ = (
        "main_flags",
        "flags",
        "header_size",
        "header_offset",
        "data_offset",
        "pack_size",
        "unpacked_size",
        "host_os",
        "file_crc",
        "file_time",
        "unpacked_version",
        "method",
        "name",
        "attributes",
    )

    def __init__(self, **fields):
        for slot in self.__slots__:
            setattr(self, slot, fields[slot])


def parse_rar_header(data: bytes | bytearray) -> RarFile:
    if len(data) < 20 or data[:7] != RAR_MAGIC:
        raise RarError("not a RAR archive")
    main_crc, main_type, main_flags, main_size = struct.unpack_from("<HBHH", data, 7)
    if main_type != MAIN_HEADER or main_size < 13 or 7 + main_size > len(data):
        raise RarError("bad RAR main header")
    if _crc16(data[9 : 7 + main_size]) != main_crc:
        raise RarError("bad RAR main header checksum")

    offset = 7 + main_size
    while offset + 7 <= len(data):
        header_crc, header_type, header_flags, header_size = struct.unpack_from(
            "<HBHH", data, offset
        )
        if header_size < 7 or offset + header_size > len(data):
            raise RarError(f"bad RAR block at {offset:#x}")
        if header_type == END_BLOCK:
            break
        if header_type != FILE_HEADER:
            packed = struct.unpack_from("<I", data, offset + 7)[0] if header_size >= 11 else 0
            offset += header_size + (packed if header_flags & DATA_FOLLOWS else 0)
            continue
        if header_size < 32:
            raise RarError(f"short RAR file header at {offset:#x}")
        if _crc16(data[offset + 2 : offset + header_size]) != header_crc:
            raise RarError(f"bad RAR file header checksum at {offset:#x}")
        pack_size, unpacked_size = struct.unpack_from("<II", data, offset + 7)
        name_size = struct.unpack_from("<H", data, offset + 26)[0]
        if 32 + name_size > header_size:
            raise RarError(f"bad RAR name size at {offset:#x}")
        return RarFile(
            main_flags=main_flags,
            flags=header_flags,
            header_size=header_size,
            header_offset=offset,
            data_offset=offset + header_size,
            pack_size=pack_size,
            unpacked_size=unpacked_size,
            host_os=data[offset + 15],
            file_crc=struct.unpack_from("<I", data, offset + 16)[0],
            file_time=struct.unpack_from("<I", data, offset + 20)[0],
            unpacked_version=data[offset + 24],
            method=data[offset + 25],
            name=data[offset + 32 : offset + 32 + name_size],
            attributes=struct.unpack_from("<I", data, offset + 28)[0],
        )
    raise RarError("no RAR file header")


def parse_rar(data: bytes | bytearray, require_data: bool = True) -> RarFile:
    rar = parse_rar_header(data)
    if require_data and rar.header_offset + rar.header_size + rar.pack_size > len(data):
        raise RarError("packed data past the end of the archive")
    return rar


def rebuild_rar(data: bytes | bytearray, rar: RarFile, payload: bytes | bytearray) -> bytes:
    header = bytearray(data[rar.header_offset : rar.header_offset + rar.header_size])
    flags = struct.unpack_from("<H", header, 3)[0]
    struct.pack_into("<H", header, 3, flags & ~ENCRYPTED)
    struct.pack_into("<H", header, 0, _crc16(header[2:]))
    end = rar.header_offset + rar.header_size + rar.pack_size
    return bytes(data[: rar.header_offset] + bytes(header) + payload + data[end:])


class ScriptEntry:
    __slots__ = ("file", "install_type", "target_path", "disk")

    def __init__(self, file, install_type, target_path, disk):
        self.file = file
        self.install_type = install_type
        self.target_path = target_path
        self.disk = disk


def parse_script(text: str) -> list[ScriptEntry]:
    entries: list[ScriptEntry] = []
    block: dict[str, str] | None = None
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith(";"):
            continue
        if line.startswith("[") and line.endswith("]"):
            continue
        lowered = line.lower()
        if lowered == "begin":
            block = {}
            continue
        if lowered == "end":
            if block is not None:
                file_name = block.get("file")
                install_type = block.get("installtype", "").lower()
                target_path = block.get("targetpath", "")
                if not file_name or install_type not in ("copy", "decompress"):
                    raise InstallerError(f"bad script block: {block!r}")
                if not target_path:
                    raise InstallerError(f"missing target path for {file_name}")
                entries.append(
                    ScriptEntry(file_name, install_type, target_path, block.get("disk", "1"))
                )
            block = None
            continue
        if block is None or "=" not in line:
            continue
        key, value = line.split("=", 1)
        block[key.strip().lower()] = value.strip()
    return entries


def resolve_target(root: Path, target_path: str, file_name: str) -> Path:
    if file_name in ("", ".", "..") or "/" in file_name or "\\" in file_name:
        raise InstallerError(f"unsafe file name: {file_name!r}")
    path = target_path.strip()
    marker = "%install_path%"
    if path.lower().startswith(marker):
        path = path[len(marker) :]
    path = path.replace("\\", "/").strip("/")
    parts = [part for part in path.split("/") if part not in ("", ".")]
    if ".." in parts:
        raise InstallerError(f"unsafe target path: {target_path!r}")
    return Path(root).joinpath(*parts, file_name)


def find_source(name: str, directories: list[Path]) -> Path | None:
    for directory in directories:
        candidate = directory / name
        if candidate.is_file():
            return candidate
        if not directory.is_dir():
            continue
        lowered = name.lower()
        for child in directory.iterdir():
            if child.is_file() and child.name.lower() == lowered:
                return child
    return None


def build_dat_map(data_dir: Path, names: list[str]) -> dict[str, tuple[Path, int]]:
    lowered = [name.lower() for name in names]
    if len(set(lowered)) != len(lowered):
        raise InstallerError("duplicate decompress names in the script")
    mapping: dict[str, tuple[Path, int]] = {}
    problems: list[str] = []
    for path in sorted(Path(data_dir).glob("*.dat")):
        try:
            hash_value = int(path.stem)
        except ValueError:
            problems.append(f"{path.name}: non-numeric chunk name")
            continue
        head = path.read_bytes()[:1024]
        if len(head) < 4:
            problems.append(f"{path.name}: truncated")
            continue
        size = struct.unpack_from("<I", head, 0)[0]
        for name in lowered:
            key = xor_key(size, hash_value, name)
            plain = xor_bytes(head, key)
            try:
                rar = parse_rar_header(plain)
            except RarError:
                continue
            if rar.name.lower() != name.encode("latin1"):
                continue
            if name in mapping:
                raise InstallerError(
                    f"chunk {path.name} and {mapping[name][0].name} both map to {name}"
                )
            mapping[name] = (path, hash_value)
            break
    missing = [name for name in lowered if name not in mapping]
    if missing:
        raise InstallerError(f"no chunk for: {', '.join(missing[:10])}")
    if problems:
        raise InstallerError("; ".join(problems[:10]))
    return mapping


def extract_rar(rar_bytes: bytes, unrar: str, target: Path, expected_size: int) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(prefix="bo2-", suffix=".rar")
    try:
        with os.fdopen(handle, "wb") as stream:
            stream.write(rar_bytes)
        with open(target, "wb") as stream:
            result = subprocess.run(
                [unrar, "p", "-idq", "-y", temporary],
                stdout=stream,
                stderr=subprocess.PIPE,
            )
        if result.returncode != 0:
            target.unlink(missing_ok=True)
            message = result.stderr.decode("latin1", "replace").strip()
            raise InstallerError(f"unrar failed for {target}: {message}")
        size = target.stat().st_size
        if size != expected_size:
            target.unlink(missing_ok=True)
            raise InstallerError(f"{target}: expected {expected_size} bytes, got {size}")
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


def _python_decryptor(sbox: bytes, password: bytes):
    cipher = Cipher(sbox, password)

    def decrypt_python(data):
        cipher.reset()
        return cipher.decrypt(bytes(data))

    return decrypt_python


def _decryptor(args, sbox: bytes, password: bytes, workdir: Path):
    if args.cipher == "python":
        return _python_decryptor(sbox, password), None

    source = Path(__file__).with_name("bo2cipher.c")
    helper = None
    if source.is_file():
        helper = compile_cipher_helper(source, workdir / "libbo2cipher.so")
    if helper is None:
        if args.cipher == "native":
            raise InstallerError("could not build bo2cipher.c (set CC or use --cipher python)")
        print("warning: C helper unavailable, using the slow Python cipher", file=sys.stderr)
        return _python_decryptor(sbox, password), None

    def decrypt_native(data):
        buffer = bytearray(data)
        helper.decrypt_inplace(buffer, sbox, password)
        return buffer

    return decrypt_native, helper


def install(args) -> int:
    script = parse_script(Path(args.script).read_text(encoding="latin1"))
    if args.only:
        wanted = args.only.lower()
        script = [entry for entry in script if entry.file.lower() == wanted]
        if not script:
            raise InstallerError(f"no script entry named {args.only}")

    decompress = [entry for entry in script if entry.install_type == "decompress"]
    copies = [entry for entry in script if entry.install_type == "copy"]
    dat_map = build_dat_map(Path(args.data), [entry.file for entry in decompress]) if decompress else {}
    if args.dry_run:
        for entry in script:
            target = resolve_target(Path(args.out), entry.target_path, entry.file)
            source = dat_map.get(entry.file.lower(), ("-", 0))[0] if entry.install_type == "decompress" else "-"
            print(f"{entry.install_type:10s} {str(source):24s} -> {target}")
        return 0

    sbox, password = load_installer(
        Path(args.installer), args.sbox_offset, args.password_offset
    )
    workdir = Path(tempfile.mkdtemp(prefix="bo2-install-"))
    decrypt, helper = _decryptor(args, sbox, password, workdir)
    unrar = args.unrar or shutil.which("unrar")
    if unrar is None:
        raise InstallerError("unrar not found; pass --unrar")
    out_root = Path(args.out)

    try:
        for entry in copies:
            source = find_source(entry.file, [Path(args.sfxi), Path(args.iso)])
            if source is None:
                raise InstallerError(f"copy source not found: {entry.file}")
            target = resolve_target(out_root, entry.target_path, entry.file)
            if args.skip_existing and target.is_file() and target.stat().st_size == source.stat().st_size:
                print(f"skip    {entry.file}")
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            print(f"copy    {entry.file} -> {target}")

        total = len(decompress)
        for index, entry in enumerate(decompress, 1):
            name = entry.file.lower()
            chunk, hash_value = dat_map[name]
            target = resolve_target(out_root, entry.target_path, entry.file)

            if args.skip_existing and target.is_file():
                head = chunk.read_bytes()[:1024]
                size = struct.unpack_from("<I", head, 0)[0]
                plain = xor_bytes(head, xor_key(size, hash_value, name))
                rar = parse_rar_header(plain)
                if target.stat().st_size == rar.unpacked_size:
                    print(f"skip    [{index}/{total}] {entry.file}")
                    continue

            raw = bytearray(chunk.read_bytes())
            size = struct.unpack_from("<I", raw, 0)[0]
            key = xor_key(size, hash_value, name)
            if helper is not None:
                helper.xor_inplace(raw, key)
                plain = raw
            else:
                plain = xor_bytes(bytes(raw), key)

            rar = parse_rar(plain)
            if rar.name.lower() != name.encode("latin1"):
                raise InstallerError(f"{chunk.name}: expected {name}, got {rar.name!r}")
            if rar.header_size != 32 + len(rar.name):
                raise InstallerError(f"{chunk.name}: unsupported extra RAR header fields")
            if rar.header_offset + rar.header_size + rar.pack_size != len(plain):
                raise InstallerError(f"{chunk.name}: unexpected trailing data")

            payload = plain[rar.header_offset + rar.header_size :]
            if rar.flags & ENCRYPTED:
                payload = decrypt(payload)
            rebuilt = rebuild_rar(plain, rar, bytes(payload))
            extract_rar(rebuilt, unrar, target, rar.unpacked_size)
            print(f"unpack  [{index}/{total}] {entry.file} -> {target} ({rar.unpacked_size} bytes)")
            del raw, plain, payload, rebuilt
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Materialize a Blood Omen 2 install tree")
    parser.add_argument("install_path", nargs="?", help="install root (same as --out)")
    parser.add_argument("--script", default="external/installer/sfxi/script.txt")
    parser.add_argument("--installer", default="external/iso/install2.dat")
    parser.add_argument("--data", default="external/iso/data")
    parser.add_argument("--sfxi", default="external/installer/sfxi")
    parser.add_argument("--iso", default="external/iso")
    parser.add_argument("--out", default="external/installed")
    parser.add_argument("--unrar")
    parser.add_argument("--only", help="extract only this script file name")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--skip-existing", action="store_true")
    parser.add_argument("--cipher", choices=("auto", "native", "python"), default="auto")
    parser.add_argument("--sbox-offset", type=lambda value: int(value, 0), default=SBOX_OFFSET)
    parser.add_argument("--password-offset", type=lambda value: int(value, 0), default=PASSWORD_OFFSET)
    args = parser.parse_args()
    if args.install_path:
        args.out = args.install_path
    try:
        return install(args)
    except (InstallerError, OSError) as error:
        print(f"install: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
