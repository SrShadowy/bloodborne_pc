# SPDX-License-Identifier: GPL-2.0-or-later
import shutil
import tempfile
import unittest
from pathlib import Path

from scripts.grimoire_tool import inspect_grimoire, cmd_pack, cmd_unpack, cmd_verify
import argparse

class TestGrimoireTool(unittest.TestCase):
    def setUp(self):
        self.test_dir = Path(tempfile.mkdtemp(prefix="bbport_test_grimoire_"))
        self.cache_dir = Path("user/cache/CUSA03173")

    def tearDown(self):
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_inspect_valid_grimoire(self):
        if not (self.cache_dir / "hunter_insight.idx").exists():
            self.skipTest("Active grimoire not present in user/cache/CUSA03173")
        info = inspect_grimoire(self.cache_dir)
        self.assertEqual(info["title_id"], "CUSA03173")
        self.assertEqual(info["version"], 1)
        self.assertGreaterEqual(info["shader_count"], 0)
        self.assertGreaterEqual(info["pipeline_count"], 0)
        self.assertIn("hunter_insight.idx", info["hashes"])

    def test_pack_and_unpack_cycle(self):
        if not (self.cache_dir / "hunter_insight.idx").exists():
            self.skipTest("Active grimoire not present in user/cache/CUSA03173")
        archive = self.test_dir / "test.bbgrimoire"
        dest_dir = self.test_dir / "unpacked"

        args_pack = argparse.Namespace(cache_dir=str(self.cache_dir), output=str(archive))
        self.assertEqual(cmd_pack(args_pack), 0)
        self.assertTrue(archive.is_file())

        args_unpack = argparse.Namespace(archive=str(archive), cache_dir=str(dest_dir))
        self.assertEqual(cmd_unpack(args_unpack), 0)

        args_verify = argparse.Namespace(cache_dir=str(dest_dir))
        self.assertEqual(cmd_verify(args_verify), 0)

if __name__ == "__main__":
    unittest.main()
