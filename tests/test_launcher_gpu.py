# SPDX-FileCopyrightText: Copyright 2026 bbport contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The launcher's GPU choice: kept by name, passed to the game as its index (BB_GPU_ID)."""
from paths import ROOT
import importlib.util
import os
import subprocess
import sys
import types
import unittest
from unittest import mock

# The launcher's window needs GTK; these tests use its functions only.
gi = types.ModuleType("gi")
gi.require_version = lambda *_args: None
gi.repository = mock.MagicMock()
# Real classes for the window's bases: LauncherWindow also inherits the page mixins
# (bbport_launcher_*.py), and a mock base next to them is a metaclass conflict.
gi.repository.Adw.ApplicationWindow = type("ApplicationWindow", (), {})
gi.repository.Adw.Application = type("Application", (), {})
sys.modules.setdefault("gi", gi)
sys.modules.setdefault("gi.repository", gi.repository)
sys.path.insert(0, str(ROOT / "launcher"))
spec = importlib.util.spec_from_file_location("bbport_launcher", ROOT / "launcher/bbport_launcher.py")
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)

GPUS = ("0\t0x8086\tintegrated\tIntel(R) UHD Graphics 630\t0\n"
        "1\t0x10de\tdiscrete\tNVIDIA GeForce RTX 4060 Laptop GPU\t1\n"
        "2\t0x10005\tcpu\tllvmpipe (LLVM 21.1.8, 256 bits)\t0\n")


class LauncherGpuTests(unittest.TestCase):
    def setUp(self):
        run = mock.patch.object(launcher.subprocess, "run", return_value=subprocess.CompletedProcess(
            [], 0, stdout=GPUS, stderr=""))
        self.run = run.start()
        self.addCleanup(run.stop)
        self.settings = dict(launcher.DEFAULTS, game_dir="/games/CUSA03173", user_dir="")

    def test_gpus_listed_in_vulkans_order(self):
        gpus = launcher.connected_gpus()
        self.assertEqual([(index, name, best) for index, _vendor, _kind, name, best in gpus],
                         [(0, "Intel(R) UHD Graphics 630", False),
                          (1, "NVIDIA GeForce RTX 4060 Laptop GPU", True),
                          (2, "llvmpipe (LLVM 21.1.8, 256 bits)", False)])
        self.assertEqual(self.run.call_args.args[0][1:], ["--gpus"])

    def test_chosen_gpu_passed_by_index(self):
        self.settings["gpu"] = "Intel(R) UHD Graphics 630"
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop("BB_GPU_ID", None)
            self.assertEqual(launcher.game_environment(self.settings).get("BB_GPU_ID"), "0")

    def test_auto_and_missing_gpu_leave_the_choice_to_the_port(self):
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop("BB_GPU_ID", None)
            self.assertNotIn("BB_GPU_ID", launcher.game_environment(self.settings))
            self.settings["gpu"] = "AMD Radeon RX 580"
            self.assertNotIn("BB_GPU_ID", launcher.game_environment(self.settings))

    def test_shell_gpu_id_wins(self):
        self.settings["gpu"] = "Intel(R) UHD Graphics 630"
        with mock.patch.dict(os.environ, {"BB_GPU_ID": "1"}):
            self.assertEqual(launcher.game_environment(self.settings)["BB_GPU_ID"], "1")

    def test_cache_static_extract_callable_game_dir(self):
        import bbport_cache
        from pathlib import Path

        class FakeLauncher(bbport_cache.CacheMixin):
            def __init__(self):
                self.settings = {"game_dir": "/games/CUSA03173", "user_dir": "/tmp/test_user"}
                self.static_catalog_button = mock.MagicMock()
                self.toasts = mock.MagicMock()

            def game_dir(self):
                return Path(self.settings["game_dir"])

            def user_dir(self):
                return Path(self.settings["user_dir"])

        fl = FakeLauncher()
        with mock.patch("subprocess.Popen") as mock_popen, mock.patch("threading.Thread") as mock_thread:
            # Capture the thread target worker and run it directly
            def run_sync(target=None, **kwargs):
                t = mock.MagicMock()
                t.start = lambda: target()
                return t
            mock_thread.side_effect = run_sync

            mock_proc = mock.MagicMock()
            mock_proc.stdout = []
            mock_proc.returncode = 0
            mock_popen.return_value = mock_proc

            with mock.patch.object(Path, "is_dir", return_value=True):
                fl.on_extract_static_catalog(None)

            cmd = mock_popen.call_args[0][0]
            self.assertIn("scripts/extract_static_shaders.py", cmd[1])
            self.assertIn("--game-dir", cmd)
            self.assertIn("/games/CUSA03173", cmd)
            self.assertIn("--output", cmd)
            self.assertIn("/tmp/test_user/cache/CUSA03173/static_shader_catalog.json", cmd)


if __name__ == "__main__":
    unittest.main()

