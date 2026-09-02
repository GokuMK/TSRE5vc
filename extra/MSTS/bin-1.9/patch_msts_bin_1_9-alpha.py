#!/usr/bin/env python3
"""Patch the verified MSTS Bin 1.8.052113 executable for custom terrain.

The mandatory terrain profile supports terrain sample grids up to 1024x1024,
patch grids up to 32x32, and patch meshes up to 32x32 samples.  It includes
the corresponding Route Editor height-edit bitmap and seam-limit changes and
enlarges both visible-patch lists from 4,096 to 16,384 entries.

Optional switches add the validated native ``-editroute:ROUTE_FOLDER`` command
and an editor-guarded 1280x800 Route Editor window/render/mouse configuration.

This file is self-contained and uses only the Python standard library.  It is
hash locked to the unmodified, non-widescreen MSTS Bin 1.8.052113 train.exe,
checks every complete instruction it changes, verifies the complete output,
and never overwrites a file unless ``--force`` is supplied.
"""

from __future__ import annotations

import argparse
import base64
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import stat
import struct
import sys
import tempfile
import zlib


SOURCE_SIZE = 4_091_953
SOURCE_SHA256 = "69218fce876298c684a2140c7d3925a452c47bb10037ffd8c491f65c5c0c6e7a"
N512_INTERMEDIATE_SHA256 = (
    "d3fcf1b971969341885f7d4168dcd3382d7184c5a6b06dd0efbbbf678a3f82a0"
)
DIRECT_ROUTE_INTERMEDIATE_SHA256 = (
    "ba0b37c1ffc7ac50170147ff614d607132d7e27eebd00502803f06bf07be251b"
)

IMAGE_BASE = 0x00400000
EXPECTED_INITIAL_SIZE_OF_IMAGE = 0x00481000
FILE_AND_SECTION_ALIGNMENT = 0x1000

LIST_ENTRY_SIZE = 8
LIST_ENTRIES = 16_384
LIST_SIZE = LIST_ENTRY_SIZE * LIST_ENTRIES
LIST_SECTION_SIZE = LIST_SIZE * 2

DIRECT_ROUTE_SECTION_RVA = 0x00481000
DIRECT_ROUTE_SECTION_VA = IMAGE_BASE + DIRECT_ROUTE_SECTION_RVA
DIRECT_ROUTE_SECTION_SIZE = 0x1000

WINDOW_WIDTH = 1280
WINDOW_HEIGHT = 800
EDITOR_MODE_FLAG = 0x007BE0F8
EDITOR_WINDOW_SECTION_SIZE = 0x1000

# Hashes are filled for every supported option combination.  They make the
# entire result, rather than only the individual patch sites, self-verifying.
EXPECTED_OUTPUTS: dict[tuple[bool, bool], tuple[int, str]] = {
    # (direct route, 1280x800): (size, SHA-256)
    (False, False): (
        4_358_144,
        "810ac9d4c21eb493ad9172ffbb499b25ea9bba13ef09e982b3a1f8e5812eaf19",
    ),
    (False, True): (
        4_362_240,
        "484e534521a25b46d680a46871852d8653096daeae91b295e750d00d45ac4eeb",
    ),
    (True, False): (
        4_362_240,
        "5f5f398bb7abf6a58e2609b945f01d7df12e52cdaaed417255785bd3cce328ce",
    ),
    (True, True): (
        4_366_336,
        "13e1104718d5f0da78deb3f334e37828424c31937be237e99d49228fe7ef9a8f",
    ),
}


@dataclass(frozen=True)
class Patch:
    name: str
    va: int
    before: bytes
    after: bytes


def imm32(value: int) -> bytes:
    return struct.pack("<I", value)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


class PELayout:
    def __init__(self, data: bytes) -> None:
        if len(data) < 0x40 or data[:2] != b"MZ":
            raise ValueError("input is not an MZ executable")

        self.pe = struct.unpack_from("<I", data, 0x3C)[0]
        if self.pe + 24 > len(data) or data[self.pe : self.pe + 4] != b"PE\0\0":
            raise ValueError("input has no valid PE signature")

        self.coff = self.pe + 4
        self.number_of_sections = struct.unpack_from("<H", data, self.coff + 2)[0]
        optional_size = struct.unpack_from("<H", data, self.coff + 16)[0]
        self.optional = self.coff + 20
        if self.optional + optional_size > len(data):
            raise ValueError("truncated PE optional header")
        if struct.unpack_from("<H", data, self.optional)[0] != 0x10B:
            raise ValueError("expected a PE32 executable")

        self.image_base = struct.unpack_from("<I", data, self.optional + 28)[0]
        self.section_alignment = struct.unpack_from("<I", data, self.optional + 32)[0]
        self.file_alignment = struct.unpack_from("<I", data, self.optional + 36)[0]
        self.size_of_image = struct.unpack_from("<I", data, self.optional + 56)[0]
        self.size_of_headers = struct.unpack_from("<I", data, self.optional + 60)[0]
        self.section_table = self.optional + optional_size

        self.sections: list[tuple[bytes, int, int, int, int]] = []
        for index in range(self.number_of_sections):
            header = self.section_table + index * 40
            if header + 40 > len(data):
                raise ValueError("truncated PE section table")
            name = data[header : header + 8].rstrip(b"\0")
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
                "<IIII", data, header + 8
            )
            self.sections.append(
                (name, virtual_address, virtual_size, raw_offset, raw_size)
            )

    def va_to_file_offset(self, va: int, length: int, file_size: int) -> int:
        if va < self.image_base:
            raise ValueError(f"VA 0x{va:08x} precedes the image base")
        rva = va - self.image_base
        if rva < self.size_of_headers:
            offset = rva
        else:
            for _, section_rva, virtual_size, raw_offset, raw_size in self.sections:
                if section_rva <= rva < section_rva + max(virtual_size, raw_size):
                    delta = rva - section_rva
                    if delta + length > raw_size:
                        raise ValueError(f"VA 0x{va:08x} is not file-backed")
                    offset = raw_offset + delta
                    break
            else:
                raise ValueError(f"VA 0x{va:08x} has no PE section mapping")
        if offset + length > file_size:
            raise ValueError(f"VA 0x{va:08x} maps beyond the end of the file")
        return offset


def apply_patches(
    source: bytes, patches: tuple[Patch, ...]
) -> tuple[bytes, list[tuple[Patch, int]]]:
    layout = PELayout(source)
    patched = bytearray(source)
    applied: list[tuple[Patch, int]] = []
    occupied: set[int] = set()

    for item in patches:
        if len(item.before) != len(item.after):
            raise ValueError(f"non-size-preserving patch: {item.name}")
        offset = layout.va_to_file_offset(item.va, len(item.before), len(source))
        item_bytes = set(range(offset, offset + len(item.before)))
        if occupied & item_bytes:
            raise ValueError(f"overlapping patch at VA 0x{item.va:08x}")
        occupied |= item_bytes
        actual = bytes(patched[offset : offset + len(item.before)])
        if actual != item.before:
            raise ValueError(
                f"unexpected bytes for {item.name} at VA 0x{item.va:08x}: "
                f"expected {item.before.hex(' ')}, found {actual.hex(' ')}"
            )
        patched[offset : offset + len(item.after)] = item.after
        applied.append((item, offset))
    return bytes(patched), applied


def append_section(
    source: bytes,
    name: bytes,
    body: bytes,
    characteristics: int,
    *,
    count_as_code: bool,
    count_as_initialized_data: bool,
) -> tuple[bytes, int, int]:
    layout = PELayout(source)
    if layout.image_base != IMAGE_BASE:
        raise ValueError(f"unexpected image base 0x{layout.image_base:08x}")
    if (
        layout.section_alignment != FILE_AND_SECTION_ALIGNMENT
        or layout.file_alignment != FILE_AND_SECTION_ALIGNMENT
    ):
        raise ValueError("unexpected PE section/file alignment")
    if not (1 <= len(name) <= 8):
        raise ValueError("PE section names must contain 1 to 8 bytes")
    if len(body) == 0 or len(body) % layout.file_alignment:
        raise ValueError("section body must be nonempty and file-aligned")

    first_raw = min(section[3] for section in layout.sections)
    new_header = layout.section_table + layout.number_of_sections * 40
    if new_header + 40 > first_raw:
        raise ValueError("no room for another PE section header")

    section_rva = layout.size_of_image
    if section_rva % layout.section_alignment:
        raise ValueError("SizeOfImage is not section-aligned")
    raw_offset = align_up(len(source), layout.file_alignment)

    patched = bytearray(source)
    patched.extend(b"\0" * (raw_offset - len(patched)))
    patched.extend(body)

    struct.pack_into(
        "<H", patched, layout.coff + 2, layout.number_of_sections + 1
    )
    if count_as_code:
        old_size = struct.unpack_from("<I", patched, layout.optional + 4)[0]
        struct.pack_into("<I", patched, layout.optional + 4, old_size + len(body))
    if count_as_initialized_data:
        old_size = struct.unpack_from("<I", patched, layout.optional + 8)[0]
        struct.pack_into("<I", patched, layout.optional + 8, old_size + len(body))
    struct.pack_into(
        "<I",
        patched,
        layout.optional + 56,
        align_up(section_rva + len(body), layout.section_alignment),
    )

    patched[new_header : new_header + 8] = name.ljust(8, b"\0")
    struct.pack_into(
        "<IIIIIIHHI",
        patched,
        new_header + 8,
        len(body),
        section_rva,
        len(body),
        raw_offset,
        0,
        0,
        0,
        0,
        characteristics,
    )
    return bytes(patched), section_rva, raw_offset


def stride_patch(va: int, register_opcode: str, old_shift: int, new_shift: int,
                 operation: str) -> Patch:
    return Patch(
        f"terrain-edit bitmap stride ({operation})",
        va,
        bytes.fromhex(f"c1 {register_opcode} {old_shift:02x}"),
        bytes.fromhex(f"c1 {register_opcode} {new_shift:02x}"),
    )


RENDER_N512_PATCHES = (
    Patch("per-patch vertex-cache allocation", 0x006BD8FD,
          bytes.fromhex("b9 84 04 00 00"), bytes.fromhex("b9 04 11 00 00")),
    Patch("fallback mesh constructor R: 16 -> 32", 0x006BD942,
          bytes.fromhex("ba 10 00 00 00"), bytes.fromhex("ba 20 00 00 00")),
    Patch("per-patch vertex-cache free size", 0x006BD9F0,
          bytes.fromhex("ba 84 04 00 00"), bytes.fromhex("ba 04 11 00 00")),
    Patch("fallback mesh destructor R: 16 -> 32", 0x006BDA17,
          bytes.fromhex("ba 10 00 00 00"), bytes.fromhex("ba 20 00 00 00")),
    Patch("terrain registration maximum R: 16 -> 32", 0x006BDE74,
          bytes.fromhex("83 7f 68 10"), bytes.fromhex("83 7f 68 20")),
    Patch("terrain registration maximum N: 256 -> 512", 0x006BDE83,
          bytes.fromhex("3d 00 01 00 00"), bytes.fromhex("3d 00 02 00 00")),
    Patch("second registration maximum R: 16 -> 32", 0x006EE23B,
          bytes.fromhex("83 7a 68 10"), bytes.fromhex("83 7a 68 20")),
    Patch("second registration maximum N: 256 -> 512", 0x006EE253,
          bytes.fromhex("81 78 6c 00 01 00 00"),
          bytes.fromhex("81 78 6c 00 02 00 00")),
    Patch("terrain render threshold A: 0x600 -> 0x1800", 0x0070A344,
          bytes.fromhex("81 fa 00 06 00 00"), bytes.fromhex("81 fa 00 18 00 00")),
    Patch("terrain render threshold B: 0x600 -> 0x1800", 0x0070A644,
          bytes.fromhex("81 fa 00 06 00 00"), bytes.fromhex("81 fa 00 18 00 00")),
    Patch("terrain render threshold C: 0x600 -> 0x1800", 0x0070A802,
          bytes.fromhex("81 fa 00 06 00 00"), bytes.fromhex("81 fa 00 18 00 00")),
    Patch("terrain render threshold D: 0x600 -> 0x1800", 0x0070ABB2,
          bytes.fromhex("81 fa 00 06 00 00"), bytes.fromhex("81 fa 00 18 00 00")),
)

N512_EDITOR_PATCHES = (
    Patch("terrain-edit bitmap: 0x2000 -> 0x8000 bytes", 0x0056DB66,
          bytes.fromhex("c7 40 14 00 20 00 00"),
          bytes.fromhex("c7 40 14 00 80 00 00")),
    Patch("terrain seam second coordinate: 256 -> 512", 0x0056E6E6,
          bytes.fromhex("81 7d 08 00 01 00 00"),
          bytes.fromhex("81 7d 08 00 02 00 00")),
    Patch("terrain seam first coordinate: 256 -> 512", 0x0056E6F5,
          bytes.fromhex("81 7d f0 00 01 00 00"),
          bytes.fromhex("81 7d f0 00 02 00 00")),
)

STRIDE_SITES = (
    (0x0056E2A8, "e1", "brush visited-byte lookup"),
    (0x0056E2C1, "e1", "brush visited-bit lookup"),
    (0x0056E35E, "e2", "brush visited-byte store"),
    (0x0056E370, "e1", "brush visited-bit store"),
    (0x0056E388, "e2", "brush visited-byte store address"),
    (0x0056EA54, "e1", "slope recursion visited-byte lookup"),
    (0x0056EA6D, "e1", "slope recursion visited-bit lookup"),
    (0x005701D1, "e1", "smoothing visited-byte lookup"),
    (0x005701EA, "e1", "smoothing visited-bit lookup"),
    (0x00570266, "e0", "smoothing visited-byte store"),
    (0x00570278, "e1", "smoothing visited-bit store"),
    (0x00570290, "e1", "smoothing visited-byte store address"),
    (0x00570A22, "e1", "flag-edit visited-byte lookup"),
    (0x00570A3B, "e1", "flag-edit visited-bit lookup"),
    (0x00570AB7, "e0", "flag-edit visited-byte store"),
    (0x00570AC9, "e1", "flag-edit visited-bit store"),
    (0x00570AE1, "e1", "flag-edit visited-byte store address"),
)

N512_STRIDE_PATCHES = tuple(
    stride_patch(va, opcode, 8, 9, operation)
    for va, opcode, operation in STRIDE_SITES
)

N1024_EDITOR_PATCHES = (
    Patch("terrain registration maximum N: 512 -> 1024", 0x006BDE83,
          bytes.fromhex("3d 00 02 00 00"), bytes.fromhex("3d 00 04 00 00")),
    Patch("second registration maximum N: 512 -> 1024", 0x006EE253,
          bytes.fromhex("81 78 6c 00 02 00 00"),
          bytes.fromhex("81 78 6c 00 04 00 00")),
    Patch("terrain-edit bitmap: 0x8000 -> 0x20000 bytes", 0x0056DB66,
          bytes.fromhex("c7 40 14 00 80 00 00"),
          bytes.fromhex("c7 40 14 00 00 02 00")),
    Patch("terrain seam second coordinate: 512 -> 1024", 0x0056E6E6,
          bytes.fromhex("81 7d 08 00 02 00 00"),
          bytes.fromhex("81 7d 08 00 04 00 00")),
    Patch("terrain seam first coordinate: 512 -> 1024", 0x0056E6F5,
          bytes.fromhex("81 7d f0 00 02 00 00"),
          bytes.fromhex("81 7d f0 00 04 00 00")),
) + tuple(
    stride_patch(va, opcode, 9, 10, operation)
    for va, opcode, operation in STRIDE_SITES
)


DIRECT_ROUTE_PAYLOAD_B85 = (
    "c-jl?eA@g*p)>SFr|+BKu<-6uj?RzWt~|{@IQg5p7#J9OMHxgo7y@>10@*Jbfkfxwh}PRBML-3mxrQeV"
    "zctqiFqCk0yNYxhbh}D)bM)GZ05zl?XJ=q&Jy~MZ`mIE-`6pA6NLsU<QYl|rbB*6WrBb%$V@%C2nHqmG"
    "GcqugvF-#>f6_iLH9U~E`Z((a&uRvs07HCS^bQc+`6l*|2#EL6y!+sPpesAibzbj02sB=oe>)rhcJbyP"
    "jzt{JKcdQ+n-3^7{{nG-fjEa0EH9J_fb2TX<oWtM*s#}3h6jK~9iA`>KwzheS}DT|OVJ(w7Yig*Go&?x"
    "qmQ9P<2dV&2assUV%(($q`G-e0ORFFHv5793#}TP7y|x_Zn)dR@NyH=f&W9-|8NgS1cH*kEK6fG12BEt"
    "b{kvXD=`PAUgPdMj@JJrV%`1{zy!zND+*Hn!Uz~3tp`e3y3Jl+Pdm;EHiDt^2QXnZA7MER7Xk7kLqR!="
    "<7MRHyZ?jvw=?o@mqJahzd@YeAkJY0%NwP_tp`dtnh!EHA7Xla0i0r^4;vncKFmNHkjjw4kjYTOP{feW"
    "Pzt0|8Aib<7zLwX6pVsVFbYP&C>RC96#xKKmA{q"
)
DIRECT_ROUTE_PAYLOAD_SHA256 = (
    "8009a8158b442c6fb537e3caabc98246847c449dcec836ff767a44666a61ccac"
)


def direct_route_payload() -> bytes:
    payload = zlib.decompress(base64.b85decode(DIRECT_ROUTE_PAYLOAD_B85))
    if len(payload) != DIRECT_ROUTE_SECTION_SIZE:
        raise ValueError("embedded direct-route payload has an invalid size")
    if sha256(payload) != DIRECT_ROUTE_PAYLOAD_SHA256:
        raise ValueError("embedded direct-route payload failed its SHA-256 check")
    return payload


def relative_jump(source_va: int, target_va: int, size: int) -> bytes:
    if size < 5:
        raise ValueError("a near jump needs at least five bytes")
    displacement = target_va - (source_va + 5)
    return b"\xe9" + struct.pack("<i", displacement) + b"\x90" * (size - 5)


def add_direct_route(source: bytes) -> bytes:
    layout = PELayout(source)
    if layout.size_of_image != DIRECT_ROUTE_SECTION_RVA:
        raise ValueError(
            f"direct-route section expected SizeOfImage 0x{DIRECT_ROUTE_SECTION_RVA:x}, "
            f"found 0x{layout.size_of_image:x}"
        )
    hooks = (
        Patch("direct-route command parser hook", 0x00499BDD,
              bytes.fromhex("55 8b ec 83 ec 20"),
              relative_jump(0x00499BDD, DIRECT_ROUTE_SECTION_VA + 0x000, 6)),
        Patch("direct-route chooser hook", 0x004814DF,
              bytes.fromhex("b9 24 26 75 00 e8 5a 12 f8 ff"),
              relative_jump(0x004814DF, DIRECT_ROUTE_SECTION_VA + 0x200, 10)),
        Patch("direct-route route-dialog hook", 0x0065B6C0,
              bytes.fromhex("55 8b ec 83 ec 08"),
              relative_jump(0x0065B6C0, DIRECT_ROUTE_SECTION_VA + 0x300, 6)),
    )
    hooked, _ = apply_patches(source, hooks)
    output, section_rva, _ = append_section(
        hooked,
        b".editrt",
        direct_route_payload(),
        0xE0000060,
        count_as_code=True,
        count_as_initialized_data=True,
    )
    if section_rva != DIRECT_ROUTE_SECTION_RVA:
        raise ValueError("direct-route section was placed at an unexpected RVA")
    return output


def visible_list_patches(list_a_va: int, list_b_va: int) -> tuple[Patch, ...]:
    return (
        Patch("visible-patch list B draw base", 0x006BF627,
              bytes.fromhex("ba 70 57 83 00"), b"\xba" + imm32(list_b_va)),
        Patch("visible-patch list A draw base", 0x006BF63F,
              bytes.fromhex("ba 70 d7 82 00"), b"\xba" + imm32(list_a_va)),
        Patch("visible-patch list B auxiliary-pass base", 0x006BF90D,
              bytes.fromhex("be 70 57 83 00"), b"\xbe" + imm32(list_b_va)),
        Patch("visible-patch list A producer pointer 1", 0x006F11F4,
              bytes.fromhex("89 3c c5 70 d7 82 00"),
              bytes.fromhex("89 3c c5") + imm32(list_a_va)),
        Patch("visible-patch list A producer pointer 2", 0x006F11FB,
              bytes.fromhex("89 34 c5 74 d7 82 00"),
              bytes.fromhex("89 34 c5") + imm32(list_a_va + 4)),
        Patch("visible-patch list B producer pointer 1", 0x006F1237,
              bytes.fromhex("89 3c c5 70 57 83 00"),
              bytes.fromhex("89 3c c5") + imm32(list_b_va)),
        Patch("visible-patch list B producer pointer 2", 0x006F123E,
              bytes.fromhex("89 34 c5 74 57 83 00"),
              bytes.fromhex("89 34 c5") + imm32(list_b_va + 4)),
    )


WINDOW_STATIC_PATCHES = (
    Patch("editor device width: 640 -> 1280", 0x005211D6,
          bytes.fromhex("c7 45 f0 80 02 00 00"),
          bytes.fromhex("c7 45 f0") + imm32(WINDOW_WIDTH)),
    Patch("editor device height: 480 -> 800", 0x005211DD,
          bytes.fromhex("c7 45 f4 e0 01 00 00"),
          bytes.fromhex("c7 45 f4") + imm32(WINDOW_HEIGHT)),
    Patch("Route Editor client height: 480 -> 800", 0x00493CC7,
          b"\xba" + imm32(480), b"\xba" + imm32(WINDOW_HEIGHT)),
    Patch("Route Editor client width: 640 -> 1280", 0x00493CCC,
          b"\xb9" + imm32(640), b"\xb9" + imm32(WINDOW_WIDTH)),
    Patch("mouse/pick logical width: 640.0 -> 1280.0", 0x00770560,
          struct.pack("<f", 640.0), struct.pack("<f", float(WINDOW_WIDTH))),
    Patch("mouse/pick logical height: 480.0 -> 800.0", 0x0077055C,
          struct.pack("<f", 480.0), struct.pack("<f", float(WINDOW_HEIGHT))),
)


def build_graphics_request_stub(stub_va: int, continue_va: int) -> bytes:
    """Choose 1280x800 only while MSTS's existing editor flag is active."""
    code = bytearray()
    code += b"\x6a\x01"  # push 1, preserving the original third argument
    code += b"\x83\x3d" + imm32(EDITOR_MODE_FLAG) + b"\x00"
    code += b"\x74\x0f"  # je normal_request

    code += b"\x68" + imm32(WINDOW_HEIGHT)
    code += b"\xba" + imm32(WINDOW_WIDTH)
    code += relative_jump(stub_va + len(code), continue_va, 5)

    code += b"\x68" + imm32(480)
    code += b"\xba" + imm32(640)
    code += relative_jump(stub_va + len(code), continue_va, 5)
    if len(code) != 41:
        raise ValueError("unexpected editor graphics-request stub size")
    return bytes(code)


def add_guarded_editor_window(source: bytes) -> bytes:
    layout = PELayout(source)
    section_rva = layout.size_of_image
    section_va = IMAGE_BASE + section_rva
    primary_stub_va = section_va
    retry_stub_va = section_va + 0x40
    primary_stub = build_graphics_request_stub(primary_stub_va, 0x005210BF)
    retry_stub = build_graphics_request_stub(retry_stub_va, 0x005210E5)

    original_request = bytes.fromhex(
        "6a 01 68 e0 01 00 00 ba 80 02 00 00"
    )
    hooks = (
        Patch(
            "editor-guarded primary graphics request",
            0x005210B3,
            original_request,
            relative_jump(0x005210B3, primary_stub_va, len(original_request)),
        ),
        Patch(
            "editor-guarded retry graphics request",
            0x005210D9,
            original_request,
            relative_jump(0x005210D9, retry_stub_va, len(original_request)),
        ),
    )
    output, _ = apply_patches(source, hooks + WINDOW_STATIC_PATCHES)

    body = bytearray(EDITOR_WINDOW_SECTION_SIZE)
    body[: len(primary_stub)] = primary_stub
    body[0x40 : 0x40 + len(retry_stub)] = retry_stub
    output, actual_rva, _ = append_section(
        output,
        b".editwin",
        bytes(body),
        0x60000020,
        count_as_code=True,
        count_as_initialized_data=False,
    )
    if actual_rva != section_rva:
        raise ValueError("editor-window section was placed at an unexpected RVA")
    return output


def build(source: bytes, direct_route: bool, window_1280x800: bool) -> bytes:
    output, _ = apply_patches(
        source,
        RENDER_N512_PATCHES + N512_EDITOR_PATCHES + N512_STRIDE_PATCHES,
    )
    if sha256(output) != N512_INTERMEDIATE_SHA256:
        raise ValueError("N512/R32 intermediate output failed its SHA-256 check")

    if direct_route:
        output = add_direct_route(output)
        if sha256(output) != DIRECT_ROUTE_INTERMEDIATE_SHA256:
            raise ValueError("direct-route intermediate output failed its SHA-256 check")

    layout = PELayout(output)
    list_section_rva = layout.size_of_image
    list_a_va = IMAGE_BASE + list_section_rva
    list_b_va = list_a_va + LIST_SIZE
    output, _ = apply_patches(
        output,
        N1024_EDITOR_PATCHES + visible_list_patches(list_a_va, list_b_va),
    )
    output, actual_rva, _ = append_section(
        output,
        b".p32lst",
        b"\0" * LIST_SECTION_SIZE,
        0xC0000040,
        count_as_code=False,
        count_as_initialized_data=True,
    )
    if actual_rva != list_section_rva:
        raise ValueError("visible-patch-list section was placed at an unexpected RVA")

    if window_1280x800:
        output = add_guarded_editor_window(output)
    return output


def atomic_write(path: Path, data: bytes, mode: int, force: bool) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and not force:
        raise FileExistsError(f"output already exists: {path}")
    temporary_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", prefix=f".{path.name}.", dir=path.parent, delete=False
        ) as temporary:
            temporary_name = temporary.name
            temporary.write(data)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.chmod(temporary_name, stat.S_IMODE(mode))
        os.replace(temporary_name, path)
        temporary_name = None
    finally:
        if temporary_name is not None:
            Path(temporary_name).unlink(missing_ok=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="unmodified Bin 1.8.052113 train.exe")
    parser.add_argument(
        "output",
        type=Path,
        nargs="?",
        help="output executable (default: train.terrain-patched.exe beside source)",
    )
    parser.add_argument(
        "--direct-route",
        action="store_true",
        help="add the native -editroute:ROUTE_FOLDER command",
    )
    parser.add_argument(
        "--window-1280x800",
        action="store_true",
        help="set Route Editor window, render target, and mouse map to 1280x800",
    )
    parser.add_argument(
        "--full",
        action="store_true",
        help="enable both --direct-route and --window-1280x800",
    )
    parser.add_argument(
        "--force", action="store_true", help="replace an existing output file"
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    direct_route = args.direct_route or args.full
    window_1280x800 = args.window_1280x800 or args.full
    output_path = args.output
    if output_path is None:
        output_path = args.source.with_name(
            args.source.stem + ".terrain-patched" + args.source.suffix
        )
    if args.source.resolve() == output_path.resolve():
        raise ValueError("source and output paths must differ")

    source = args.source.read_bytes()
    source_hash = sha256(source)
    if len(source) != SOURCE_SIZE:
        raise ValueError(
            f"unsupported source size: expected {SOURCE_SIZE}, found {len(source)}"
        )
    if source_hash != SOURCE_SHA256:
        raise ValueError(
            f"unsupported source SHA-256 {source_hash}; expected {SOURCE_SHA256}"
        )
    initial_layout = PELayout(source)
    if initial_layout.size_of_image != EXPECTED_INITIAL_SIZE_OF_IMAGE:
        raise ValueError(
            f"unexpected source SizeOfImage 0x{initial_layout.size_of_image:x}"
        )

    output = build(source, direct_route, window_1280x800)
    output_hash = sha256(output)
    expected_size, expected_hash = EXPECTED_OUTPUTS[
        (direct_route, window_1280x800)
    ]
    if len(output) != expected_size or output_hash != expected_hash:
        raise ValueError(
            "complete output verification failed: "
            f"expected {expected_size} bytes/{expected_hash}, "
            f"found {len(output)} bytes/{output_hash}"
        )

    atomic_write(output_path, output, args.source.stat().st_mode, args.force)
    features = ["N1024/P32/R32 terrain and editing"]
    if direct_route:
        features.append("direct route launch")
    if window_1280x800:
        features.append("1280x800 editor")
    print(f"source={args.source}")
    print(f"source_sha256={source_hash}")
    print(f"output={output_path}")
    print(f"output_size={len(output)}")
    print(f"output_sha256={output_hash}")
    print(f"features={'; '.join(features)}")
    print("status=success; the source executable was not modified")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
