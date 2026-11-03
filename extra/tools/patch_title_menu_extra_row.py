#!/usr/bin/env python3
"""
Title Menu Extra Row Patcher for Bloodborne (BBPort / shadPS4).

Expands the title menu CommandList in Scaleform files (title.gfx, title_dlc.gfx,
title_dlc_eu.gfx) by adding a 6th row (Item_5_0).
This allows the "Exit Game (title menu)" patch to be fully visible without
requiring the player to scroll down.
"""

import os
import sys
import struct
import argparse
import shutil
from pathlib import Path

ITEM_DELTA_Y = 797  # twips (approx 39.85 px)
ITEM_TX = -10283    # twips (-514.15 px)
ITEM_BASE_Y = 2240  # twips of Item_4_0 (112.0 px)
ITEM5_TY = ITEM_BASE_Y + ITEM_DELTA_Y  # 3037 twips (151.85 px)

def encode_matrix(tx, ty):
    tx_bits = f"{(1 << 15) + tx:015b}" if tx < 0 else f"{tx:015b}"
    ty_bits = f"{(1 << 15) + ty:015b}" if ty < 0 else f"{ty:015b}"
    full_bits = "0001111" + tx_bits + ty_bits + "000"
    return bytes(int(full_bits[i:i+8], 2) for i in range(0, 40, 8))

def patch_title_gfx_data(data: bytes) -> bytes:
    if len(data) < 21:
        raise ValueError("Invalid GFX/SWF file: file too short")
    magic = data[:3]
    if magic not in (b"GFX", b"FWS"):
        raise ValueError(f"Unsupported header magic: {magic}")

    if b"Item_5_0\x00" in data:
        # Already patched
        return data

    nbits = data[8] >> 3
    rect_bytes = (5 + 4 * nbits + 7) // 8
    pos = 8 + rect_bytes + 4

    while pos < len(data):
        th = struct.unpack("<H", data[pos:pos+2])[0]
        tt = th >> 6
        tl = th & 0x3F
        hsz = 2
        if tl == 0x3F:
            tl = struct.unpack("<I", data[pos+2:pos+6])[0]
            hsz = 6

        tag_data = data[pos+hsz : pos+hsz+tl]
        if tt == 39:  # DefineSprite
            sid = struct.unpack("<H", tag_data[:2])[0]
            if b"Item_0_0" in tag_data and b"Item_4_0" in tag_data:
                # Character 31 is the menu item movieclip
                matrix_bytes = encode_matrix(ITEM_TX, ITEM5_TY)
                item5_payload = (
                    bytes([0x26]) +            # Flags: HasCharacter | HasMatrix | HasName
                    struct.pack("<H", 1) +     # Depth 1
                    struct.pack("<H", 31) +    # Character ID 31
                    matrix_bytes +             # Matrix
                    b"Item_5_0\x00"            # Name
                )
                item5_tag = struct.pack("<H", (26 << 6) | 0x3F) + struct.pack("<I", len(item5_payload)) + item5_payload

                new_sprite_body = bytearray(tag_data[:4])  # Sprite ID + Frame Count
                new_sprite_body += item5_tag               # Item_5_0 at depth 1

                # Re-depth existing items (+5 to depth)
                s_off = 4
                while s_off < len(tag_data):
                    sth = struct.unpack("<H", tag_data[s_off:s_off+2])[0]
                    stt = sth >> 6
                    stl = sth & 0x3F
                    shsz = 2
                    if stl == 0x3F:
                        stl = struct.unpack("<I", tag_data[s_off+2:s_off+6])[0]
                        shsz = 6
                    s_body = bytearray(tag_data[s_off+shsz : s_off+shsz+stl])

                    if stt == 26:  # PlaceObject2
                        old_depth = struct.unpack("<H", s_body[1:3])[0]
                        s_body[1:3] = struct.pack("<H", old_depth + 5)

                    if shsz == 6:
                        new_sprite_body += struct.pack("<H", (stt << 6) | 0x3F)
                        new_sprite_body += struct.pack("<I", len(s_body))
                    else:
                        new_sprite_body += struct.pack("<H", (stt << 6) | len(s_body))
                    new_sprite_body += s_body
                    s_off += shsz + stl

                new_sprite_tag = bytearray()
                if hsz == 6:
                    new_sprite_tag += struct.pack("<H", (tt << 6) | 0x3F)
                    new_sprite_tag += struct.pack("<I", len(new_sprite_body))
                else:
                    new_sprite_tag += struct.pack("<H", (tt << 6) | len(new_sprite_body))
                new_sprite_tag += new_sprite_body

                patched = bytearray(data[:pos]) + new_sprite_tag + bytearray(data[pos+hsz+tl:])
                patched[4:8] = struct.pack("<I", len(patched))
                return bytes(patched)

        pos += hsz + tl

    raise ValueError("Target CommandList sprite containing Item_0_0..Item_4_0 not found")

def main():
    parser = argparse.ArgumentParser(description="Patch Bloodborne title.gfx files to add a 6th menu row.")
    parser.add_argument("--game-dir", default="/run/media/shadowy/SSD Kingspec/ps4_games/CUSA03173",
                        help="Path to CUSA03173 game directory")
    parser.add_argument("--out-dir", default=None,
                        help="Output directory to place dvdroot_ps4/menu/*.gfx")
    parser.add_argument("--in-place", action="store_true",
                        help="Directly replace game dvdroot_ps4/menu/*.gfx (with .orig backup)")
    args = parser.parse_args()

    game_dir = Path(args.game_dir)
    menu_dir = game_dir / "dvdroot_ps4" / "menu"
    if not menu_dir.is_dir():
        print(f"Error: Menu directory not found at {menu_dir}", file=sys.stderr)
        sys.exit(1)

    filenames = ["title.gfx", "title_dlc.gfx", "title_dlc_eu.gfx"]
    patched_files = {}

    for fname in filenames:
        src = menu_dir / fname
        if not src.is_file():
            print(f"Warning: {src} not found, skipping")
            continue
        data = src.read_bytes()
        patched = patch_title_gfx_data(data)
        patched_files[fname] = patched
        status = "already patched" if patched == data else "patched successfully"
        print(f"Processed {fname}: {status} ({len(data)} -> {len(patched)} bytes)")

    if args.in_place:
        print("\nApplying in-place modifications to game directory:")
        for fname, data in patched_files.items():
            dst = menu_dir / fname
            bak = menu_dir / f"{fname}.orig"
            if not bak.exists():
                shutil.copy2(dst, bak)
                print(f"  Created backup: {bak.name}")
            dst.write_bytes(data)
            print(f"  Updated: {dst.name}")

    if args.out_dir:
        out_base = Path(args.out_dir) / "dvdroot_ps4" / "menu"
        out_base.mkdir(parents=True, exist_ok=True)
        print(f"\nWriting mod files to: {out_base}")
        for fname, data in patched_files.items():
            dst = out_base / fname
            dst.write_bytes(data)
            print(f"  Wrote: {dst}")

    print("\nDone! All 6 menu rows are enabled.")

if __name__ == "__main__":
    main()
