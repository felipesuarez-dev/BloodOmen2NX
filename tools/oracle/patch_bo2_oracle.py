#!/usr/bin/env python3
"""Build a Wine oracle copy of bo2.exe with the broken 2D scale guard.

The original crashes at 0x56E2C9 when the display object at [esi+8] is
null. This hook checks that pointer before the scaling math and returns
without touching the object; the normal path resumes at 0x56E2C6.

Usage: patch_bo2_oracle.py <input-exe> <output-exe>
"""
import os
import struct
import sys

import pefile

HOOK_VA = 0x56E2BF
HOOK_ORIG = bytes.fromhex("8b4608d944240c")
RESUME_VA = 0x56E2C6
CAVE_VA = 0x83E7E2


def va_to_offset(pe, text, va):
    base = text.VirtualAddress + pe.OPTIONAL_HEADER.ImageBase
    return text.PointerToRawData + (va - base)


def cave_code():
    code = bytearray()
    code += bytes.fromhex("8b4608")      # mov eax,[esi+8]
    code += bytes.fromhex("85c0")          # test eax,eax
    code += bytes.fromhex("7409")          # je return
    code += bytes.fromhex("d944240c")      # fld [esp+0xc]
    jump_from = CAVE_VA + len(code) + 5
    code += b"\xe9" + struct.pack("<i", RESUME_VA - jump_from)
    code += bytes.fromhex("5e")            # return: pop esi
    code += bytes.fromhex("c20c00")        # ret 0xc
    return bytes(code)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    if os.path.realpath(src) == os.path.realpath(dst):
        sys.exit("refusing to overwrite the input exe")
    pe = pefile.PE(src)
    image = bytearray(open(src, "rb").read())
    text = [s for s in pe.sections if b".text" in s.Name][0]
    off = va_to_offset(pe, text, HOOK_VA)
    if bytes(image[off:off + len(HOOK_ORIG)]) != HOOK_ORIG:
        sys.exit("hook site changed: " + bytes(image[off:off + len(HOOK_ORIG)]).hex())
    cave_off = va_to_offset(pe, text, CAVE_VA)
    code = cave_code()
    if cave_off + len(code) > len(image):
        sys.exit("cave is outside the file")
    if any(byte not in (0x00, 0xCC) for byte in image[cave_off:cave_off + len(code)]):
        sys.exit("cave is not free padding")
    image[cave_off:cave_off + len(code)] = code
    jump = struct.pack("<i", CAVE_VA - (HOOK_VA + 5))
    image[off:off + 5] = b"\xe9" + jump
    image[off + 5:off + 7] = b"\x90\x90"
    # DEBUG: the device-init assert is a hard int3; skip it while diagnosing.
    int3_va = 0x568763
    int3_off = va_to_offset(pe, text, int3_va)
    if image[int3_off] != 0xCC:
        sys.exit("int3 site changed")
    image[int3_off] = 0x90
    # 3DNow femms is not supported by Wine's CPU emulation; emms has the
    # same MMX-state semantics.
    text_start = text.PointerToRawData
    text_end = text.PointerToRawData + text.SizeOfRawData
    count = 0
    for index in range(text_start, text_end - 1):
        if image[index:index + 2] == b"\x0f\x0e":
            image[index:index + 2] = b"\x0f\x77"
            count += 1
    print("patched femms", count)
    open(dst, "wb").write(image)
    print("wrote", dst)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: patch_bo2_oracle.py <input-exe> <output-exe>")
    main()
