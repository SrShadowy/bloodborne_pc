#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import datetime
import os
import tempfile
import unittest
from pathlib import Path

import scripts.bump_version as bump_version


class TestBumpVersion(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        (self.root / "VERSION").write_text("0.6.6-8\n", encoding="utf-8")
        (self.root / ".build_date").write_text("2026-10-26\n", encoding="utf-8")

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_parse_version(self):
        self.assertEqual(bump_version.parse_version("0.6.6-8"), ("0.6", 6, 8))
        self.assertEqual(bump_version.parse_version("0.6.5-dev-24"), ("0.6", 5, 24))
        self.assertEqual(bump_version.parse_version("0.6.6"), ("0.6", 6, 0))
        self.assertEqual(bump_version.parse_version("0.6"), ("0.6", 0, 0))
        self.assertEqual(bump_version.parse_version("1.0.0-0"), ("1.0", 0, 0))

    def test_increment_same_day(self):
        # Today is 2026-10-26, same as .build_date
        today = datetime.date(2026, 10, 26)
        next_ver, cur_ver = bump_version.bump(root_dir=self.root, target_date=today, verbose=False)
        self.assertEqual(cur_ver, "0.6.6-8")
        self.assertEqual(next_ver, "0.6.6-9")

        # Verify files were updated
        self.assertEqual((self.root / "VERSION").read_text(encoding="utf-8").strip(), "0.6.6-9")
        self.assertEqual((self.root / ".build_date").read_text(encoding="utf-8").strip(), "2026-10-26")

        # Run another build on the same day -> 0.6.6-10
        next_ver, _ = bump_version.bump(root_dir=self.root, target_date=today, verbose=False)
        self.assertEqual(next_ver, "0.6.6-10")
        self.assertEqual((self.root / "VERSION").read_text(encoding="utf-8").strip(), "0.6.6-10")

    def test_increment_next_day(self):
        # Tomorrow is 2026-10-27
        tomorrow = datetime.date(2026, 10, 27)
        next_ver, cur_ver = bump_version.bump(root_dir=self.root, target_date=tomorrow, verbose=False)
        self.assertEqual(cur_ver, "0.6.6-8")
        # Day resets build counter to 0 and increments day from 6 to 7
        self.assertEqual(next_ver, "0.6.7-0")

        # Verify files
        self.assertEqual((self.root / "VERSION").read_text(encoding="utf-8").strip(), "0.6.7-0")
        self.assertEqual((self.root / ".build_date").read_text(encoding="utf-8").strip(), "2026-10-27")

        # Second build on the new day -> 0.6.7-1
        next_ver, _ = bump_version.bump(root_dir=self.root, target_date=tomorrow, verbose=False)
        self.assertEqual(next_ver, "0.6.7-1")
        self.assertEqual((self.root / "VERSION").read_text(encoding="utf-8").strip(), "0.6.7-1")

    def test_dry_run_does_not_modify_files(self):
        today = datetime.date(2026, 10, 26)
        next_ver, cur_ver = bump_version.bump(root_dir=self.root, dry_run=True, target_date=today, verbose=False)
        self.assertEqual(cur_ver, "0.6.6-8")
        self.assertEqual(next_ver, "0.6.6-9")
        self.assertEqual((self.root / "VERSION").read_text(encoding="utf-8").strip(), "0.6.6-8")

    def test_missing_build_date_fallback(self):
        (self.root / ".build_date").unlink()
        today = datetime.date(2026, 10, 26)
        # Should gracefully detect or default to today without throwing
        next_ver, _ = bump_version.bump(root_dir=self.root, target_date=today, verbose=False)
        self.assertTrue(next_ver.startswith("0.6."))
    def test_set_version(self):
        bump_version.set_version("0.7.0-0", root_dir=self.root, target_date=datetime.date(2026, 11, 1), verbose=False)
        self.assertEqual((self.root / "VERSION").read_text(encoding="utf-8").strip(), "0.7.0-0")
        self.assertEqual((self.root / ".build_date").read_text(encoding="utf-8").strip(), "2026-11-01")

        # Now incrementing on same day (2026-11-01) should produce 0.7.0-1
        next_ver, _ = bump_version.bump(root_dir=self.root, target_date=datetime.date(2026, 11, 1), verbose=False)
        self.assertEqual(next_ver, "0.7.0-1")

    def test_cli_current(self):
        import subprocess
        res = subprocess.run(
            ["python3", "scripts/bump_version.py", "--root", str(self.root), "--current"],
            capture_output=True,
            text=True,
            check=True
        )
        self.assertEqual(res.stdout.strip(), "0.6.6-8")

    def test_guards_in_build_sh_logic(self):
        import subprocess
        # Create a mini bash runner that mirrors the check in build.sh
        bash_script = """
        set -euo pipefail
        for arg in "$@"; do
            if [[ "$arg" == "--no-bump" ]]; then
                export BB_SKIP_VERSION_INC=1
                break
            fi
        done
        if [[ -z "${BB_SKIP_VERSION_INC:-}" && -z "${BB_APPIMAGE_VARIANT:-}" ]]; then
            python3 scripts/bump_version.py --root "$TEST_ROOT" --quiet
        fi
        """
        script_file = self.root / "mini_build.sh"
        script_file.write_text(bash_script, encoding="utf-8")

        # 1. With --no-bump: version should not change
        subprocess.run(["bash", str(script_file), "--no-bump"], env=dict(os.environ, TEST_ROOT=str(self.root)), check=True)
        self.assertEqual((self.root / "VERSION").read_text(encoding="utf-8").strip(), "0.6.6-8")

        # 2. With BB_SKIP_VERSION_INC=1: version should not change
        subprocess.run(["bash", str(script_file)], env=dict(os.environ, TEST_ROOT=str(self.root), BB_SKIP_VERSION_INC="1"), check=True)
        self.assertEqual((self.root / "VERSION").read_text(encoding="utf-8").strip(), "0.6.6-8")

        # 3. With BB_APPIMAGE_VARIANT=default: version should not change (AppImage build protection)
        subprocess.run(["bash", str(script_file)], env=dict(os.environ, TEST_ROOT=str(self.root), BB_APPIMAGE_VARIANT="default"), check=True)
        self.assertEqual((self.root / "VERSION").read_text(encoding="utf-8").strip(), "0.6.6-8")

        # 4. Without guards: version should increment from 0.6.6-8 to 0.6.6-9
        subprocess.run(["bash", str(script_file)], env=dict(os.environ, TEST_ROOT=str(self.root)), check=True)
        self.assertEqual((self.root / "VERSION").read_text(encoding="utf-8").strip(), "0.6.6-9")


if __name__ == "__main__":
    unittest.main()


