"""Read Kain's state and the gameplay camera from the memory of a running bo2.exe.

Under Wine the game's virtual addresses are the same as the Windows ones, so
the process memory can be read through `/proc/<pid>/mem` with the addresses
found in the decompilation. Linux allows that when the reader is an ancestor
of the process (Yama `ptrace_scope` 1), which holds for the game started by
`wine_capture.py`.

Memory access sits behind `Memory.read`, so tests can pass a fake.
"""
from __future__ import annotations

import errno
import math
import os
import struct
from typing import NamedTuple

OBJECT_LIST = 0x00A836A8  # FUN_004a5290: head node of the circular list of game instances
KAIN_VTABLE = 0x00841D84  # vtable of kain::KainTopLevelBehavior (RTTI)
MAX_NODES = 100_000  # guards against a corrupt list

# "HUD" is the Kain debug HUD at 0x42a200; Ghidra splits its tail as FUN_0042a3e0.
# Every offset and its source is listed in docs/ORACLE_WINE.md, "Game state".
# Instance (FUN_004a5290).
POSITION = 0xB4  # f32 x3; HUD
YAW = 0xC8  # f32 radians, the third of the rotation triple at +0xC0; HUD
BEHAVIOR = 0x124  # u32; FUN_004d9850 (GetPlayerHitPoints)
# Kain behavior (kain::KainTopLevelBehavior).
VELOCITY = 0x19C  # f32 x3, world frame; HUD
ON_PLATFORM = 0x4E0  # u32; HUD
# KainState (behavior+0x128) +0x458: the animation index passed to the animation play call
# right after it is stored (FUN_00442800, FUN_004436e0).
ANIMATION = 0x128 + 0x458  # u32
STATS = 0x128 + 0x594  # u32 pointer; the HUD passes ECX=behavior+0x128 to FUN_00425fb0
# Stats.
HEALTH = 0x11C  # u32 pointer; FUN_00425fb0
LORE = 0x124  # u32 pointer; FUN_00425fc0
HEALTH_CURRENT = 0xC  # f32; FUN_004d9850
HEALTH_MAX = 0x14  # f32; HUD (0x42a696)
LORE_CURRENT = 0x4  # f32; FUN_00425fc0 and the HUD "Cur Lore" print

# Camera. GameCamera (RTTI .?AVGameCamera@@) is a 0x180-byte object whose
# constructor FUN_00409b50 stores a second vtable at +0x18; nothing global
# points to it, so it is found by that vtable in the heap.
GAME_CAMERA_VTABLE2 = 0x0083F770  # vtable at GameCamera+0x18
GAME_CAMERA_VTABLE2_OFFSET = 0x18
FOLLOW_CAMERA = 0x68  # u32; FollowingCreatureCameraImpl, 0x114 bytes (FUN_00406de0)
FOLLOW_CAMERA_VTABLE = 0x0083F5E4  # vtable of FollowingCreatureCameraImpl (RTTI)
# Observed live, not read in the decompilation: the followed creature's instance.
# It only picks and validates the camera; it is never emitted.
FOLLOW_TARGET = 0x4
# FUN_004088c0 (the follow camera update) reads a camera-to-world matrix at +0xC
# (f32 x16): the translation is elements 12-14 (+0x3C) and the type is the u32
# at +0xC+0x40 (+0x4C). The translation is valid when the type is 4 or 8. Row 0
# as the forward vector was only checked live.
CAMERA_MATRIX = 0xC
MATRIX_TYPE = 0x40


class Kain(NamedTuple):
    instance: int
    behavior: int


class Memory:
    """Address-space reader. Subclasses implement `read`."""

    def read(self, address: int, size: int) -> bytes:
        """Return exactly `size` bytes, or raise OSError if they cannot be read."""
        raise NotImplementedError

    def regions(self) -> list[tuple[int, int]]:
        """Writable (start, end) ranges to search for heap objects."""
        return []

    def alive(self) -> bool:
        """False once the process is gone, so a failed read is not retried forever."""
        return True

    def u32(self, address: int) -> int:
        return struct.unpack("<I", self.read(address, 4))[0]

    def f32s(self, address: int, count: int = 1) -> tuple[float, ...]:
        return struct.unpack(f"<{count}f", self.read(address, 4 * count))


class ProcessMemory(Memory):
    """Memory of a live process, through `/proc/<pid>/mem`."""

    def __init__(self, pid: int):
        self.pid = pid
        self.fd = os.open(f"/proc/{pid}/mem", os.O_RDONLY)

    def regions(self) -> list[tuple[int, int]]:
        ranges = []
        with open(f"/proc/{self.pid}/maps", encoding="ascii") as maps:
            for line in maps:
                span, perms = line.split()[:2]
                start, end = (int(value, 16) for value in span.split("-"))
                if perms.startswith("rw") and end <= 0x80000000:  # the 32-bit game address space
                    ranges.append((start, end))
        return ranges

    def alive(self) -> bool:
        return os.path.exists(f"/proc/{self.pid}")

    def read(self, address: int, size: int) -> bytes:
        data = os.pread(self.fd, size, address)
        if len(data) != size:
            raise OSError(errno.EIO, f"short read of {size} bytes at {address:#x}")
        return data

    def close(self) -> None:
        os.close(self.fd)


def find_kain(mem: Memory) -> Kain | None:
    """Walk the instance list and return Kain, or None if he does not exist yet.

    A list node is `[next, prev, instance]`. An instance is Kain when its
    behavior has the vtable of kain::KainTopLevelBehavior. Raises OSError if
    the list itself cannot be read.
    """
    head = mem.u32(OBJECT_LIST)
    node = mem.u32(head) if head else 0
    for _ in range(MAX_NODES):
        if not node or node == head:
            break
        instance = mem.u32(node + 8)
        try:
            behavior = mem.u32(instance + BEHAVIOR)
            if mem.u32(behavior) == KAIN_VTABLE:
                return Kain(instance, behavior)
        except OSError:
            pass  # not a readable instance; try the next one
        node = mem.u32(node)
    return None


def is_kain(mem: Memory, kain: Kain) -> bool:
    """True while `kain` is still the live Kain. Raises OSError if a pointer cannot be read.

    Freed heap objects stay mapped, so a reload could leave a frozen copy that
    still reads fine; the vtable and the instance's behavior pointer give it away.
    """
    return mem.u32(kain.behavior) == KAIN_VTABLE and mem.u32(kain.instance + BEHAVIOR) == kain.behavior


def read_state(mem: Memory, kain: Kain) -> dict:
    """Read one sample of Kain's state. Raises OSError if a pointer is invalid."""
    stats = mem.u32(kain.behavior + STATS)
    health = mem.u32(stats + HEALTH)
    lore = mem.u32(stats + LORE)
    return {
        "pos": mem.f32s(kain.instance + POSITION, 3),
        "yaw": mem.f32s(kain.instance + YAW)[0],
        "vel": mem.f32s(kain.behavior + VELOCITY, 3),
        "on_platform": mem.u32(kain.behavior + ON_PLATFORM),
        "anim": mem.u32(kain.behavior + ANIMATION),
        "health": mem.f32s(health + HEALTH_CURRENT)[0],
        "health_max": mem.f32s(health + HEALTH_MAX)[0],
        "lore": mem.f32s(lore + LORE_CURRENT)[0],
    }


def is_follow_camera(mem: Memory, follow: int, kain_instance: int) -> bool:
    """True if `follow` is a follow camera that follows `kain_instance`. Raises OSError if unreadable."""
    return mem.u32(follow) == FOLLOW_CAMERA_VTABLE and mem.u32(follow + FOLLOW_TARGET) == kain_instance


def find_follow_camera(mem: Memory, kain_instance: int) -> int | None:
    """Return the FollowingCreatureCameraImpl that follows Kain, or None if there is none yet."""
    needle = struct.pack("<I", GAME_CAMERA_VTABLE2)
    for start, end in mem.regions():
        try:
            data = mem.read(start, end - start)
        except OSError:
            continue
        hit = data.find(needle)
        while hit != -1:
            if hit % 4 == 0:
                game_camera = start + hit - GAME_CAMERA_VTABLE2_OFFSET
                try:
                    follow = mem.u32(game_camera + FOLLOW_CAMERA)
                    if follow and is_follow_camera(mem, follow, kain_instance):
                        return follow
                except OSError:
                    pass
            hit = data.find(needle, hit + 1)
    return None


def read_camera(mem: Memory, follow: int) -> dict | None:
    """Read the camera eye and heading, or None while the pose is missing or not finite."""
    matrix = mem.f32s(follow + CAMERA_MATRIX, 16)
    if mem.u32(follow + CAMERA_MATRIX + MATRIX_TYPE) not in (4, 8):
        return None  # FUN_004088c0 reads the translation as zero for these types
    forward_x, forward_y, forward_z = matrix[0:3]
    eye = matrix[12:15]
    if not all(math.isfinite(value) for value in (*eye, forward_x, forward_y, forward_z)):
        return None  # a NaN would turn into pitch 90
    return {
        "eye": eye,
        "yaw_deg": math.degrees(math.atan2(forward_y, forward_x)),
        "pitch_deg": math.degrees(math.asin(max(-1.0, min(1.0, forward_z)))),
    }
