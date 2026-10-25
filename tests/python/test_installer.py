import shutil
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bo2fmt"))

import installer  # noqa: E402


def crc16(data):
    return zlib.crc32(data) & 0xFFFF


def build_rar(name, payload, plain=None, flags=0x8084, method=0x35, version=20):
    plain = payload if plain is None else plain
    name_bytes = name.encode("latin1")
    head_size = 32 + len(name_bytes)
    body = (
        struct.pack(
            "<BHHIIBIIBBHI",
            0x74,
            flags,
            head_size,
            len(payload),
            len(plain),
            2,
            zlib.crc32(plain) & 0xFFFFFFFF,
            0,
            version,
            method,
            len(name_bytes),
            0x20,
        )
        + name_bytes
    )
    file_header = struct.pack("<H", crc16(body)) + body
    main_body = struct.pack("<BHH", 0x73, 0x0008, 13) + b"\0" * 6
    main = struct.pack("<H", crc16(main_body)) + main_body
    return b"Rar!\x1a\x07\x00" + main + file_header + payload


class InstallerTest(unittest.TestCase):
    def test_deobfuscate_roundtrip(self):
        name = "PreCode.big"
        hash_value = 0x617EC21D
        size = 0x1234
        plain = b"Rar!\x1a\x07\x00" + b"payload"
        raw = installer.xor_bytes(plain, installer.xor_key(size, hash_value, name))
        self.assertEqual(struct.unpack_from("<I", raw, 0)[0], size)
        self.assertEqual(installer.deobfuscate(raw, name, hash_value), plain)

    def test_cipher_roundtrip(self):
        sbox = bytes(range(256))
        password = b"test-password"
        cipher = installer.Cipher(sbox, password)
        plain = bytes(range(64))
        encrypted = cipher.encrypt(plain)
        cipher.reset()
        self.assertEqual(cipher.decrypt(encrypted), plain)

    @unittest.skipUnless(shutil.which("cc") or shutil.which("gcc"), "no C compiler")
    def test_c_helper_matches_python(self):
        source = Path(__file__).resolve().parents[2] / "tools" / "bo2fmt" / "bo2cipher.c"
        with tempfile.TemporaryDirectory() as directory:
            helper = installer.compile_cipher_helper(source, Path(directory) / "libbo2cipher.so")
            if helper is None:
                self.skipTest("could not build the C helper")
            sbox = bytes(reversed(range(256)))
            password = b"another-password"
            plain = bytes(range(96))
            cipher = installer.Cipher(sbox, password)
            encrypted = cipher.encrypt(plain)
            decrypted = bytearray(encrypted)
            helper.decrypt_inplace(decrypted, sbox, password)
            self.assertEqual(bytes(decrypted), plain)

    def test_reads_rar_header(self):
        data = build_rar("Test.big", b"packed", plain=b"plain data")
        rar = installer.parse_rar(data)
        self.assertEqual(rar.name, b"Test.big")
        self.assertEqual(rar.header_size, 32 + len("Test.big"))
        self.assertEqual(rar.header_offset, 20)
        self.assertEqual(rar.pack_size, len(b"packed"))
        self.assertEqual(rar.unpacked_size, len(b"plain data"))
        self.assertEqual(rar.flags, 0x8084)
        self.assertEqual(rar.method, 0x35)
        self.assertEqual(rar.unpacked_version, 20)
        self.assertEqual(rar.data_offset, 20 + rar.header_size)

    def test_parse_script(self):
        text = (
            "[extern]\r\n"
            "; comment\r\n"
            "Begin\r\n\tFile\t= a.big\r\n\tInstallType\t= decompress\r\n"
            "\tTargetPath\t= %install_path%\\media\\\r\nEnd\r\n"
            "Begin\r\n\tFile = b.exe\r\n\tInstallType = copy\r\n"
            "\tTargetPath = %install_path%\\\r\nEnd\r\n"
        )
        entries = installer.parse_script(text)
        self.assertEqual([entry.file for entry in entries], ["a.big", "b.exe"])
        self.assertEqual(entries[0].install_type, "decompress")
        self.assertEqual(entries[1].install_type, "copy")

    def test_resolve_target(self):
        root = Path("/tmp/out")
        self.assertEqual(
            installer.resolve_target(root, "%install_path%\\media\\gogames\\bo2\\", "pc.rkv"),
            root / "media" / "gogames" / "bo2" / "pc.rkv",
        )
        self.assertEqual(
            installer.resolve_target(root, "%install_path%\\", "bo2.exe"),
            root / "bo2.exe",
        )
        with self.assertRaises(ValueError):
            installer.resolve_target(root, "%install_path%\\..\\evil", "x")

    def test_build_dat_map(self):
        name = "Test.big"
        hash_value = 0x12345678
        size = 0x2222
        rar = build_rar(name, b"packed", plain=b"plain data")
        raw = installer.xor_bytes(
            rar, installer.xor_key(size, hash_value, name.lower())
        )
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / f"{hash_value}.dat").write_bytes(raw)
            mapping = installer.build_dat_map(Path(directory), [name.lower()])
            self.assertEqual(mapping[name.lower()][1], hash_value)

    def test_load_installer(self):
        sbox = bytes(reversed(range(256)))
        password = b"secret"
        blob = bytearray(0x300)
        blob[0x20 : 0x20 + len(password)] = password
        blob[0x100 : 0x200] = sbox
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "install2.dat"
            path.write_bytes(blob)
            loaded_sbox, loaded_password = installer.load_installer(
                path, sbox_offset=0x100, password_offset=0x20
            )
        self.assertEqual(loaded_sbox, sbox)
        self.assertEqual(loaded_password, password)

    def test_rebuild_rar(self):
        payload = b"decrypted!"
        data = build_rar("Test.big", payload, plain=b"plain data")
        rar = installer.parse_rar(data)
        rebuilt = installer.rebuild_rar(data, rar, payload)
        parsed = installer.parse_rar(rebuilt)
        self.assertEqual(parsed.flags & 0x0004, 0)
        self.assertEqual(rebuilt[parsed.data_offset : parsed.data_offset + parsed.pack_size], payload)


if __name__ == "__main__":
    unittest.main()
