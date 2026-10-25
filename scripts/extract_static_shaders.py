#!/usr/bin/env python3
"""BBPort Static Shader Extractor (Shader Discovery Engine - Fase 1).

Extracts and catalogs all AMD GCN / Orbis shader programs directly from
Bloodborne game archive bundles (dvdroot_ps4/shader/*.shaderbnd.dcx) without
requiring game execution or GPU initialization.
"""
from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import time
import zlib

STAGE_MAP = {
    0: "Vertex",
    1: "Fragment",
    2: "Geometry",
    3: "Hull",
    4: "Domain",
    5: "Compute",
}


def decompress_dcx(data: bytes) -> bytes:
    """Decompresses a DCX file (standard DFLT / zlib format)."""
    if len(data) < 0x4C:
        raise ValueError("File is too small to be a valid DCX archive")
    if data[:4] != b"DCX\0":
        raise ValueError(f"Invalid DCX magic: {data[:4]!r}")
    if data[0x18:0x1C] != b"DCS\0" or data[0x28:0x2C] != b"DFLT":
        raise ValueError("Unsupported DCX compression mode (only DFLT is supported)")

    decomp_size, packed_size = struct.unpack_from(">II", data, 0x1C)
    decomp = zlib.decompress(data[0x4C:0x4C + packed_size])
    if len(decomp) != decomp_size:
        raise ValueError(f"Decompressed size mismatch: expected {decomp_size}, got {len(decomp)}")
    return decomp


def extract_shaders_from_bundle(decomp: bytes, bundle_name: str) -> list[dict]:
    """Scans decompressed BND4 container for OrbShdr blocks and GCN microcode."""
    shaders = []
    pos = 0
    mov_vcc_hi = struct.pack("<I", 0xBEEB03FF)

    while True:
        pos = decomp.find(b"OrbShdr", pos)
        if pos == -1:
            break

        # Header fields from BinaryInfo (regs_shader.h):
        # 0x00: signature (7 bytes: 'OrbShdr')
        # 0x07: version (1 byte)
        # 0x08: bitfield u32 (pssl_or_cg:1, cached:1, type:4, source_type:2, length:24)
        # 0x0C: usage info (4 bytes)
        # 0x10: shader_hash (uint64)
        # 0x18: crc32 (uint32)
        if pos + 28 > len(decomp):
            break

        bf, = struct.unpack_from("<I", decomp, pos + 8)
        stype = (bf >> 2) & 0xF
        stage_name = STAGE_MAP.get(stype, f"Stage_{stype}")
        code_length_header = (bf >> 8) & 0xFFFFFF

        shash, crc = struct.unpack_from("<QI", decomp, pos + 16)

        # Locate preceding s_mov_b32 vcc_hi, length instruction
        code_pos = decomp.rfind(mov_vcc_hi, max(0, pos - 0x10000), pos)
        code_bytes = b""
        code_sha256 = ""
        is_verified = False

        if code_pos != -1:
            _, dw1 = struct.unpack_from("<II", decomp, code_pos)
            expected_header = code_pos + (dw1 + 1) * 8
            if expected_header == pos:
                code_bytes = decomp[code_pos:pos]
                code_sha256 = hashlib.sha256(code_bytes).hexdigest()
                is_verified = True

        shaders.append({
            "hash": f"0x{shash:016x}",
            "hash_u64": shash,
            "crc32": f"0x{crc:08x}",
            "stage": stage_name,
            "bundle": bundle_name,
            "header_offset": pos,
            "code_offset": code_pos if is_verified else -1,
            "code_size": len(code_bytes),
            "code_size_header": code_length_header,
            "sha256": code_sha256,
            "verified_boundary": is_verified,
        })

        pos += 7

    return shaders


def scan_shader_directory(shader_dir: Path) -> dict:
    """Scans all *.shaderbnd.dcx bundles in the directory and produces a unified catalog."""
    bundles = sorted(glob.glob(os.path.join(str(shader_dir), "*.dcx")))
    if not bundles:
        raise FileNotFoundError(f"No .dcx files found in {shader_dir}")

    catalog: dict[str, dict] = {}
    bundle_summaries: dict[str, dict] = {}
    total_instances = 0

    for path in bundles:
        fname = os.path.basename(path)
        try:
            with open(path, "rb") as f:
                raw_data = f.read()
            decomp = decompress_dcx(raw_data)
        except Exception as e:
            bundle_summaries[fname] = {"status": "error", "error": str(e), "count": 0}
            continue

        extracted = extract_shaders_from_bundle(decomp, fname)
        total_instances += len(extracted)

        bundle_summaries[fname] = {
            "status": "ok",
            "count": len(extracted),
            "decompressed_size": len(decomp),
            "packed_size": len(raw_data),
        }

        for item in extracted:
            h = item["hash"]
            if h not in catalog:
                catalog[h] = {
                    "hash": h,
                    "crc32": item["crc32"],
                    "stage": item["stage"],
                    "source_bundles": [fname],
                    "code_size": item["code_size"],
                    "sha256": item["sha256"],
                    "verified": item["verified_boundary"],
                }
            else:
                if fname not in catalog[h]["source_bundles"]:
                    catalog[h]["source_bundles"].append(fname)

    return {
        "format": "bbport_static_shader_catalog_v1",
        "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "shader_dir": str(shader_dir),
        "total_instances": total_instances,
        "unique_shaders": len(catalog),
        "bundle_summaries": bundle_summaries,
        "shaders": catalog,
    }


def find_game_shader_dir(base_path: Path | None = None) -> Path:
    """Attempts to find dvdroot_ps4/shader from standard candidates."""
    candidates = []
    if base_path:
        candidates.append(base_path / "dvdroot_ps4" / "shader")
        candidates.append(base_path / "shader")
        candidates.append(base_path)

    # Standard configuration paths
    candidates.append(Path("/run/media/shadowy/SSD Kingspec/ps4_games/CUSA03173/dvdroot_ps4/shader"))
    candidates.append(Path("../CUSA03173/dvdroot_ps4/shader"))
    candidates.append(Path("dvdroot_ps4/shader"))

    for c in candidates:
        if c.is_dir() and any(c.glob("*.shaderbnd.dcx")):
            return c.resolve()

    raise FileNotFoundError("Could not automatically locate Bloodborne dvdroot_ps4/shader directory")


def main() -> int:
    parser = argparse.ArgumentParser(description="BBPort Static Shader Extractor")
    parser.add_argument("--game-dir", type=Path, default=None, help="Bloodborne game directory")
    parser.add_argument("--shader-dir", type=Path, default=None, help="Explicit path to shader folder")
    parser.add_argument("--output", type=Path, default=None, help="Output JSON path")
    parser.add_argument("--summary-only", action="store_true", help="Print summary without writing full JSON")
    parser.add_argument("--quiet", action="store_true", help="Minimal output")

    args = parser.parse_args()

    try:
        if args.shader_dir:
            shader_dir = args.shader_dir
        else:
            shader_dir = find_game_shader_dir(args.game_dir)
    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

    if not args.quiet:
        print(f"[*] Scanning shader directory: {shader_dir}")

    t0 = time.time()
    try:
        report = scan_shader_directory(shader_dir)
    except Exception as e:
        print(f"Error during scan: {e}", file=sys.stderr)
        return 1

    elapsed = time.time() - t0

    if not args.quiet:
        print(f"[+] Scan completed in {elapsed:.3f}s:")
        print(f"    Total Shader Instances: {report['total_instances']:,}")
        print(f"    Unique Shader Programs: {report['unique_shaders']:,}")
        print("    Breakdown by bundle:")
        for bname, sinfo in report["bundle_summaries"].items():
            if sinfo["status"] == "ok":
                print(f"      - {bname:35s}: {sinfo['count']:5d} shaders")
            else:
                print(f"      - {bname:35s}: FAILED ({sinfo.get('error')})")

    if args.summary_only:
        return 0

    out_path = args.output
    if not out_path:
        out_path = Path("dist/user/cache/CUSA03173/static_shader_catalog.json")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)

    if not args.quiet:
        print(f"[+] Catalog written to: {out_path} ({os.path.getsize(out_path):,} bytes)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
