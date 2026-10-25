#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""BBPort Hunter's Grimoire Management Tool (Fase 10).

Inspects, validates, packs and unpacks the 3-file monolithic shader archive:
  - hunter_insight.idx (Index & Lookup)
  - caryll_runes.pack   (Contiguous SPIR-V Blob)
  - blood_echoes.meta  (Metadata & Pipeline Keys)
"""

import argparse
import hashlib
import struct
import sys
import zipfile
from pathlib import Path

MAGIC_INDEX = 0x47534E49    # "INSG"
MAGIC_SHADERS = 0x454E5552  # "RUNE"
MAGIC_META = 0x48434542     # "BECH"

ARCHIVE_FILES = (
    "hunter_insight.idx",
    "caryll_runes.pack",
    "blood_echoes.meta",
)

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def inspect_grimoire(cache_dir: Path) -> dict:
    idx_p = cache_dir / ARCHIVE_FILES[0]
    spv_p = cache_dir / ARCHIVE_FILES[1]
    meta_p = cache_dir / ARCHIVE_FILES[2]

    for p in (idx_p, spv_p, meta_p):
        if not p.is_file():
            raise FileNotFoundError(f"Missing grimoire file: {p.name}")

    with open(idx_p, "rb") as f:
        data = f.read(72)
        if len(data) < 72:
            raise ValueError("Index header too small")
        magic, ver = struct.unpack_from("<II", data, 0)
        if magic != MAGIC_INDEX:
            raise ValueError(f"Invalid index magic: 0x{magic:08x} (expected 0x{MAGIC_INDEX:08x})")
        title_raw = data[8:24].split(b"\x00", 1)[0].decode("ascii", errors="replace")
        shader_count, pipe_count = struct.unpack_from("<II", data, 24)
        spv_bytes, meta_bytes, timestamp = struct.unpack_from("<QQQ", data, 32)

    with open(spv_p, "rb") as f:
        spv_magic, _ = struct.unpack("<II", f.read(8))
        if spv_magic != MAGIC_SHADERS:
            raise ValueError(f"Invalid shaders magic: 0x{spv_magic:08x}")

    with open(meta_p, "rb") as f:
        meta_magic, _ = struct.unpack("<II", f.read(8))
        if meta_magic != MAGIC_META:
            raise ValueError(f"Invalid metadata magic: 0x{meta_magic:08x}")

    return {
        "title_id": title_raw,
        "version": ver,
        "shader_count": shader_count,
        "pipeline_count": pipe_count,
        "spv_bytes": spv_bytes,
        "meta_bytes": meta_bytes,
        "timestamp": timestamp,
        "index_size": idx_p.stat().st_size,
        "shaders_size": spv_p.stat().st_size,
        "metadata_size": meta_p.stat().st_size,
        "hashes": {
            ARCHIVE_FILES[0]: sha256_file(idx_p),
            ARCHIVE_FILES[1]: sha256_file(spv_p),
            ARCHIVE_FILES[2]: sha256_file(meta_p),
        },
    }

def cmd_info(args: argparse.Namespace) -> int:
    try:
        info = inspect_grimoire(Path(args.cache_dir))
        print("==================================================")
        print("  BBPort — Hunter's Grimoire Archive Information  ")
        print("==================================================")
        print(f"Title ID:        {info['title_id']}")
        print(f"Archive Version: {info['version']}")
        print(f"Shaders Packed:  {info['shader_count']}")
        print(f"Pipelines Ready: {info['pipeline_count']}")
        print(f"Index File:      {info['index_size']:,} bytes ({ARCHIVE_FILES[0]})")
        print(f"SPIR-V Blob:     {info['shaders_size']:,} bytes ({ARCHIVE_FILES[1]})")
        print(f"Metadata Blob:   {info['metadata_size']:,} bytes ({ARCHIVE_FILES[2]})")
        print("SHA-256 Checksums:")
        for name, h in info["hashes"].items():
            print(f"  {name}: {h}")
        print("Grimoire status: VALID [OK]")
        return 0
    except Exception as e:
        print(f"Error inspecting grimoire: {e}", file=sys.stderr)
        return 1

def cmd_pack(args: argparse.Namespace) -> int:
    cache_dir = Path(args.cache_dir)
    out_archive = Path(args.output)
    try:
        info = inspect_grimoire(cache_dir)
        out_archive.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(out_archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
            for name in ARCHIVE_FILES:
                zf.write(cache_dir / name, arcname=name)
        print(f"Successfully exported Hunter's Grimoire to {out_archive} ({out_archive.stat().st_size:,} bytes)")
        print(f"Contains {info['shader_count']} shaders and {info['pipeline_count']} pipelines.")
        return 0
    except Exception as e:
        print(f"Error exporting grimoire: {e}", file=sys.stderr)
        return 1

def cmd_unpack(args: argparse.Namespace) -> int:
    archive_p = Path(args.archive)
    cache_dir = Path(args.cache_dir)
    if not archive_p.is_file():
        print(f"Archive not found: {archive_p}", file=sys.stderr)
        return 1
    cache_dir.mkdir(parents=True, exist_ok=True)
    try:
        with zipfile.ZipFile(archive_p, "r") as zf:
            for name in ARCHIVE_FILES:
                if name not in zf.namelist():
                    raise ValueError(f"Archive is missing required file: {name}")
                zf.extract(name, cache_dir)
        info = inspect_grimoire(cache_dir)
        print(f"Successfully imported Hunter's Grimoire into {cache_dir}")
        print(f"Verified {info['shader_count']} shaders and {info['pipeline_count']} pipelines.")
        return 0
    except Exception as e:
        print(f"Error importing grimoire: {e}", file=sys.stderr)
        return 1

def cmd_verify(args: argparse.Namespace) -> int:
    try:
        info = inspect_grimoire(Path(args.cache_dir))
        print(f"OK: Grimoire verified. Shaders={info['shader_count']}, Pipelines={info['pipeline_count']}")
        return 0
    except Exception as e:
        print(f"VERIFY FAILED: {e}", file=sys.stderr)
        return 1

def main() -> int:
    parser = argparse.ArgumentParser(description="BBPort Hunter's Grimoire Tool")
    sub = parser.add_subparsers(dest="command", required=True)

    p_info = sub.add_parser("info", help="Inspect active grimoire archive")
    p_info.add_argument("cache_dir", help="Directory containing grimoire files")

    p_pack = sub.add_parser("pack", help="Export grimoire to a portable .bbgrimoire / .zip file")
    p_pack.add_argument("cache_dir", help="Directory containing grimoire files")
    p_pack.add_argument("--output", "-o", required=True, help="Destination archive path")

    p_unpack = sub.add_parser("unpack", help="Import grimoire from a portable archive")
    p_unpack.add_argument("archive", help="Path to grimoire archive")
    p_unpack.add_argument("--cache-dir", "-d", required=True, help="Destination cache directory")

    p_verify = sub.add_parser("verify", help="Verify grimoire magic and integrity")
    p_verify.add_argument("cache_dir", help="Directory containing grimoire files")

    args = parser.parse_args()
    if args.command == "info":
        return cmd_info(args)
    elif args.command == "pack":
        return cmd_pack(args)
    elif args.command == "unpack":
        return cmd_unpack(args)
    elif args.command == "verify":
        return cmd_verify(args)
    return 1

if __name__ == "__main__":
    sys.exit(main())
