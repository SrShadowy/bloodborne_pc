#!/usr/bin/env python3
"""Unit tests for BBPort Static Shader Extractor (Fase 1)."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from scripts import extract_static_shaders as extractor


class TestStaticShaderExtractor(unittest.TestCase):
    def make_fake_dcx(self, inner_data: bytes) -> bytes:
        packed = zlib.compress(inner_data)
        decomp_size = len(inner_data)
        packed_size = len(packed)
        # DCX header (0x4C bytes)
        # 0x00: 'DCX\0', 0x18: 'DCS\0', 0x1C: (decomp_size, packed_size), 0x28: 'DFLT'
        header = bytearray(0x4C)
        header[0:4] = b"DCX\0"
        header[0x18:0x1C] = b"DCS\0"
        struct.pack_into(">II", header, 0x1C, decomp_size, packed_size)
        header[0x28:0x2C] = b"DFLT"
        return bytes(header) + packed

    def make_fake_shader(self, shader_hash: int, crc: int, stage_idx: int, code_bytes: bytes) -> bytes:
        # Preceding instruction: s_mov_b32 vcc_hi, length (dw0=0xBEEB03FF, dw1=words)
        # In BinaryInfo: (dw1 + 1) * 8 = header_offset from code_pos
        # Here code_bytes is followed immediately by OrbShdr header:
        # header_offset = len(code_bytes). Therefore (dw1 + 1) * 8 = len(code_bytes)
        # So len(code_bytes) must be a multiple of 8, and dw1 = (len(code_bytes) // 8) - 1
        pad_len = (8 - (len(code_bytes) % 8)) % 8
        code = code_bytes + b"\x00" * pad_len
        dw1 = (len(code) // 8) - 1
        code_with_instr = struct.pack("<II", 0xBEEB03FF, dw1) + code[8:]

        # BinaryInfo:
        # 0x00: signature (7 bytes: 'OrbShdr')
        # 0x07: version (1 byte: 7)
        # 0x08: bf (pssl_or_cg:1, cached:1, type:4, source_type:2, length:24)
        # 0x0C: usage info (4 bytes)
        # 0x10: shader_hash (uint64)
        # 0x18: crc32 (uint32)
        bf = (stage_idx << 2) | (len(code_with_instr) << 8)
        header = struct.pack("<7sBI4sQI", b"OrbShdr", 7, bf, b"\x00" * 4, shader_hash, crc)
        return code_with_instr + header

    def test_dcx_decompress_success(self):
        payload = b"Hello Bloodborne Shaders" * 50
        dcx_bytes = self.make_fake_dcx(payload)
        self.assertEqual(extractor.decompress_dcx(dcx_bytes), payload)

    def test_dcx_invalid_header(self):
        with self.assertRaises(ValueError):
            extractor.decompress_dcx(b"BAD_HEADER_DATA")

    def test_extract_synthetic_shader(self):
        code = b"\x00\x00\x00\x00\x00\x00\x00\x00\x11\x22\x33\x44\x55\x66\x77\x88"
        shader_bytes = self.make_fake_shader(0x123456789ABCDEF0, 0xAABBCCDD, 0, code)
        shaders = extractor.extract_shaders_from_bundle(shader_bytes, "test_bundle.dcx")
        self.assertEqual(len(shaders), 1)
        sh = shaders[0]
        self.assertEqual(sh["hash"], "0x123456789abcdef0")
        self.assertEqual(sh["crc32"], "0xaabbccdd")
        self.assertEqual(sh["stage"], "Vertex")
        self.assertTrue(sh["verified_boundary"])
        self.assertEqual(sh["code_size"], len(code))
        self.assertEqual(sh["sha256"], hashlib.sha256(sh["code_size"] * b"\x00" if False else shader_bytes[:len(code)]).hexdigest())

    def test_scan_directory_synthetic(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmppath = Path(tmpdir)
            code = b"\x00\x00\x00\x00\x00\x00\x00\x00\xaa\xbb\xcc\xdd\xee\xff\x00\x11"
            shader_data = self.make_fake_shader(0x9999888877776666, 0x11223344, 1, code)
            dcx_data = self.make_fake_dcx(shader_data)
            (tmppath / "gxffxshader.shaderbnd.dcx").write_bytes(dcx_data)

            report = extractor.scan_shader_directory(tmppath)
            self.assertEqual(report["total_instances"], 1)
            self.assertEqual(report["unique_shaders"], 1)
            self.assertIn("0x9999888877776666", report["shaders"])
            entry = report["shaders"]["0x9999888877776666"]
            self.assertEqual(entry["stage"], "Fragment")
            self.assertEqual(entry["source_bundles"], ["gxffxshader.shaderbnd.dcx"])

    def test_real_game_extraction_if_available(self):
        real_shader_dir = Path("/run/media/shadowy/SSD Kingspec/ps4_games/CUSA03173/dvdroot_ps4/shader")
        if not real_shader_dir.is_dir():
            self.skipTest("Real game shader directory not mounted")

        report = extractor.scan_shader_directory(real_shader_dir)
        self.assertGreaterEqual(report["total_instances"], 20000)
        self.assertGreaterEqual(report["unique_shaders"], 4000)
        self.assertIn("gxflvershader.shaderbnd.dcx", report["bundle_summaries"])


if __name__ == "__main__":
    unittest.main()
