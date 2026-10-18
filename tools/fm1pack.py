#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2023 Andrey Grigoryev (kagaimiq), 2026 M-VAVE.
# A Python port of FM1 Open's sdk/Package.cs (MIT), which follows jl-misctools 0a5b12d for the
# JieLi formats and ciphers. Ravel uses it unchanged in behaviour; the port is line for line.
"""Make an FM-1 update package (.fwsc) from an app image and the SDK's boot/update resources.

  fm1pack.py APP.bin RESOURCES_DIR OUT.fwsc --product FM-1_8xxxxxx
"""
import argparse
import re
import struct
from pathlib import Path

AREA, ENTRY_POINT, APP_DATA = 0x4000, 0x02000120, 0x120
FLASH_LIMIT, USER_SIZE, BT_SIZE, MINIMUM_VM = 0xFF000, 536576, 4096, 32768
PUBLIC_CODE = 0x980F


def _table():
    t = []
    for i in range(256):
        v = i << 8
        for _ in range(8):
            v = ((v << 1) ^ (0x1021 if v & 0x8000 else 0)) & 0xFFFF
        t.append(v)
    return t


CRC_TABLE = _table()


def crc(b, begin=0, length=None):
    if length is None:
        length = len(b) - begin
    v = 0
    for i in range(begin, begin + length):
        v = ((v << 8) & 0xFFFF) ^ CRC_TABLE[(v >> 8) ^ b[i]]
    return v


def cipher(b, begin, length, state):
    for i in range(begin, begin + length):
        b[i] ^= state & 0xFF
        state = ((state << 1) ^ (0x1021 if state & 0x8000 else 0)) & 0xFFFF


def round_up(size, alignment):
    return (size + alignment - 1) & -alignment


class Image:
    def __init__(self, size, fill):
        self.b = bytearray([fill]) * size

    def number(self, off, value, width):
        for n in range(width):
            self.b[off + n] = (value >> (8 * n)) & 0xFF

    def copy(self, off, data):
        self.b[off:off + len(data)] = data

    def text(self, off, s):
        self.copy(off, s.encode("ascii"))

    def checksum(self, field, off, size):
        self.number(field, crc(self.b, off, size), 2)

    def record(self, off, name, c, address, size, attributes, last, extra):
        self.b[off:off + 32] = bytes(32)
        self.number(off + 2, c, 2)
        self.number(off + 4, address, 4)
        self.number(off + 8, size, 4)
        self.number(off + 12, attributes, 2)
        self.number(off + 14, last, 2)
        for n in range(16, 32):
            self.b[off + n] = 255
        self.text(off + 16, name)
        self.b[off + 16 + len(name)] = 0
        if extra is not None:
            self.number(off + 28, extra, 4)
        self.checksum(off, off + 2, 30)


def read_resource(d, name, size):
    b = (Path(d) / name).read_bytes()
    if len(b) != size:
        raise SystemExit(f"fm1pack: unexpected FM1 resource {name}")
    return b


def encode_boot(plain):
    result = bytearray(plain)
    banks = struct.unpack_from("<H", plain, 0)[0]
    if banks < 1 or banks > len(plain) // 16:
        raise SystemExit("fm1pack: invalid boot directory")
    previous_end = banks * 16
    for i in range(banks):
        h = i * 16
        length = struct.unpack_from("<H", plain, h + 2)[0]
        start = struct.unpack_from("<i", plain, h + 8)[0]
        if (length == 0 or start < previous_end or start > len(plain) - length or
                crc(plain, h, 14) != struct.unpack_from("<H", plain, h + 14)[0] or
                crc(plain, start, length) != struct.unpack_from("<H", plain, h + 12)[0]):
            raise SystemExit("fm1pack: invalid boot bank")
        cipher(result, h, 16, 0xFFFF)
        cipher(result, start, length, 0xFFFF)
        previous_end = start + length
    return bytes(result)


def build_flash(app, d, end, vm_end):
    boot = read_resource(d, "uboot.boot", 14488)
    config = read_resource(d, "boot-config.bin", 721)
    tool = read_resource(d, "cfg_tool.bin", 383)
    image = Image(end, 255)
    settings = Image(34 + len(config), 0)
    for bit in range(16):
        settings.b[bit] = 16
        settings.b[16 + bit] = 16 ^ (255 if ((PUBLIC_CODE >> bit) & 1) == 0 else 0)
    settings.checksum(32, 0, 32)
    settings.copy(34, config)
    image.copy(0, bytes([0, 0, 0x20, 2, 0x2F, 0x10, 0x4C, 0xC9, 0, 0xF0, 0x0F, 0, 0x10, 0x10, 0xA4, 0xFF,
                         0x4E, 0x5D, 0x0B, 0x41, 0xE0, 0xCD, 0x78, 0x1D, 0xC8, 0x56, 0x41, 0x7F, 0xB3, 0x67, 0xCF, 0x9F]))
    image.checksum(0, 2, 30)
    config_at = 160 + len(boot)
    image.record(32, "uboot.boot", crc(boot), 160, len(boot), 0, 0, None)
    image.record(64, "isd_config.ini", crc(settings.b), config_at, len(settings.b), 0x8002, 0, None)
    image.record(96, "app_dir_head", 65535, AREA, 0xFFFFFFFF, 0xFF81, 0, None)
    image.record(128, "key_mac", 65535, FLASH_LIMIT, 4096, 0x0112, 65535, None)
    for p in range(0, 160, 32):
        cipher(image.b, p, 32, 65535)
    image.copy(160, encode_boot(boot))
    image.copy(config_at, settings.b)
    payload_size = APP_DATA + len(app) + len(tool)
    a = AREA + APP_DATA
    image.copy(a, app)
    image.copy(a + len(app), tool)
    image.record(AREA + 32, "app.bin", crc(app), APP_DATA, len(app), 0xFF82, 0, None)
    image.record(AREA + 64, "cfg_tool.bin", crc(tool), APP_DATA + len(app), len(tool), 0xFF82, 0, None)
    image.record(AREA + 96, "VM", 65535, end, vm_end - end, 0x8112, 0, MINIMUM_VM)
    image.record(AREA + 128, "PRCT", 65535, 0, end, 0x8292, 0, 0x45444F43)
    image.record(AREA + 160, "BTIF", 65535, vm_end, BT_SIZE, 0x8192, 1, 0x4F545541)
    image.record(AREA, "app_area_head", crc(image.b, AREA + 32, payload_size - 32), ENTRY_POINT, payload_size, 0xFF83, 0, None)
    reserved = AREA + payload_size
    image.record(reserved + 32, "USER", 65535, FLASH_LIMIT - USER_SIZE, USER_SIZE, 0x8192, 1, 0x4F545541)
    image.record(reserved, "EXT_RESERVED", crc(image.b, reserved + 32, 32), 32, 64, 0xFF93, 1, None)
    for p in range(0, payload_size + 64, 32):
        cipher(image.b, AREA + p, min(32, payload_size + 64 - p), PUBLIC_CODE ^ (p >> 2))
    return image


def wrap(flash, loader, product):
    if len(loader) < 1 or len(loader) >= 32768:
        raise SystemExit("fm1pack: invalid FM1 update loader")
    loader_at = 1024 + round_up(len(flash.b), 32)
    total = loader_at + round_up(len(loader), 32)
    ufw = Image(total, 255)
    ufw.b[0:224] = bytes(224)
    files = [flash.b, loader]
    names = ["flash.bin", "ota.bin"]
    for i, f in enumerate(files):
        r = 64 + i * 80
        ufw.number(r, 0 if i == 0 else 100, 2)
        ufw.number(r + 2, i, 2)
        ufw.number(r + 4, crc(f), 2)
        ufw.number(r + 8, 1024 if i == 0 else loader_at, 4)
        ufw.number(r + 12, len(f), 4)
        ufw.number(r + 16, round_up(len(f), 32), 4)
        ufw.text(r + 64, names[i])
        cipher(ufw.b, r, 80, 65535)
    ufw.number(4, total, 4)
    ufw.number(8, 2, 2)
    ufw.number(10, 4, 2)
    ufw.number(12, 512, 4)
    ufw.text(16, "AC791N")
    ufw.checksum(2, 64, 160)
    ufw.checksum(0, 2, 62)
    cipher(ufw.b, 0, 64, 65535)
    ufw.copy(1024, flash.b)
    ufw.copy(loader_at, loader)
    out = bytearray()
    for src in range(total):
        out.append(ufw.b[src])
        if src < 940 and (src + 1) % 47 == 0:
            idx = src // 47
            out.append((ord(product[idx]) + idx + 1) & 0xFF if idx < len(product) else 125)
    return bytes(out)


def app_limit():
    """the largest app image the FM1 layout holds"""
    return FLASH_LIMIT - USER_SIZE - BT_SIZE - MINIMUM_VM - AREA - APP_DATA - 383 - 64


def create(app, resources, output, product, user_bytes=USER_SIZE):
    if not re.fullmatch(r"FM-1_8[0-9]{6}", product or ""):
        raise SystemExit("fm1pack: invalid FM1 update identity (FM-1_8 and six digits)")
    limit = app_limit()
    if not app or len(app) > limit or user_bytes != USER_SIZE:
        raise SystemExit(f"fm1pack: the app ({len(app)} B) does not fit the FM1 layout ({limit} B)")
    image_size = round_up(AREA + APP_DATA + len(app) + 383 + 64, 4096)
    user_at = FLASH_LIMIT - USER_SIZE
    vm_end = user_at - BT_SIZE
    if image_size > vm_end - MINIMUM_VM:
        raise SystemExit("fm1pack: insufficient FM1 settings space")
    image = build_flash(app, resources, image_size, vm_end)
    package = wrap(image, (Path(resources) / "ota.bin").read_bytes(), product)
    Path(output).write_bytes(package)
    return {"FLASH_BIN_SIZE": image_size, "VM_REAL_SIZE": vm_end - image_size, "user_bytes": USER_SIZE,
            "app_bytes": len(app), "app_limit": limit, "package_bytes": len(package)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("app", type=Path)
    ap.add_argument("resources", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--product", required=True)
    a = ap.parse_args()
    print(create(a.app.read_bytes(), a.resources, a.out, a.product))


if __name__ == "__main__":
    main()
