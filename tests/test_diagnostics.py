# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests for BBPort diagnostics, structured logging, and resilient config."""
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT / "launcher") not in sys.path:
    sys.path.insert(0, str(ROOT / "launcher"))

from diagnostics import (
    LOG,
    setup_logging,
    get_log_dir,
    get_log_file,
    get_runtime_backend,
    collect_diagnostics_report,
)
import bbport_config


class TestDiagnosticsAndLogging(unittest.TestCase):
    def test_setup_logging_idempotent_and_creates_logs(self):
        log_dir = setup_logging()
        self.assertIsNotNone(log_dir)
        self.assertTrue(log_dir.is_dir())

        log_file = get_log_file()
        self.assertIsNotNone(log_file)
        self.assertEqual(log_file, log_dir / "launcher.log")

        # Test idempotency
        second_call = setup_logging()
        self.assertEqual(log_dir, second_call)

        # Check fatal-crash.log creation
        crash_log = log_dir / "fatal-crash.log"
        self.assertTrue(crash_log.exists())

    def test_collect_diagnostics_report(self):
        report = collect_diagnostics_report()
        self.assertIsInstance(report, str)
        self.assertIn("BBPort Launcher Diagnostic Report", report)
        self.assertIn("[System & Runtime]", report)
        self.assertIn("[Desktop & Display Session]", report)
        self.assertIn("[GUI Toolkits]", report)
        self.assertIn("[Storage & Paths]", report)
        self.assertIn("[Gamepad Subsytem]", report)
        self.assertIn("[Graphics & GPUs]", report)

    def test_runtime_backend_detection(self):
        class MockWaylandDisplay:
            pass
        MockWaylandDisplay.__name__ = "GdkWaylandDisplay"

        class MockX11Display:
            pass
        MockX11Display.__name__ = "GdkX11Display"

        class MockOtherDisplay:
            pass
        MockOtherDisplay.__name__ = "GdkCustomDisplay"

        self.assertIn("Wayland", get_runtime_backend(MockWaylandDisplay()))
        self.assertIn("X11", get_runtime_backend(MockX11Display()))
        self.assertEqual(get_runtime_backend(MockOtherDisplay()), "GdkCustomDisplay")
        self.assertEqual(get_runtime_backend(None), "None (No display connected)")

    def test_atomic_save_settings_and_backup(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            orig_dir = bbport_config.CONFIG_DIR
            orig_file = bbport_config.CONFIG_FILE
            try:
                bbport_config.CONFIG_DIR = Path(tmp_dir) / "bbport-test"
                bbport_config.CONFIG_FILE = bbport_config.CONFIG_DIR / "settings.json"

                # 1. Initial save
                initial_data = {"test_key": "initial_value", "theme_mode": "dark"}
                bbport_config.save_settings(initial_data)

                self.assertTrue(bbport_config.CONFIG_FILE.is_file())
                loaded = bbport_config.load_settings()
                self.assertEqual(loaded.get("test_key"), "initial_value")

                # Check permissions (0600 on POSIX)
                mode = oct(bbport_config.CONFIG_FILE.stat().st_mode & 0o777)
                self.assertEqual(mode, "0o600")

                # 2. Second save creates .bak
                updated_data = {"test_key": "updated_value", "theme_mode": "light"}
                bbport_config.save_settings(updated_data)

                bak_file = bbport_config.CONFIG_FILE.with_suffix(".json.bak")
                self.assertTrue(bak_file.is_file())
                bak_data = json.loads(bak_file.read_text(encoding="utf-8"))
                self.assertEqual(bak_data.get("test_key"), "initial_value")

                loaded_updated = bbport_config.load_settings()
                self.assertEqual(loaded_updated.get("test_key"), "updated_value")

                # 3. Simulate corruption of primary file
                bbport_config.CONFIG_FILE.write_text("{corrupt_json: invalid", encoding="utf-8")
                recovered = bbport_config.load_settings()
                # Should recover initial_value from .bak
                self.assertEqual(recovered.get("test_key"), "initial_value")

            finally:
                bbport_config.CONFIG_DIR = orig_dir
                bbport_config.CONFIG_FILE = orig_file


    def test_launcher_app_startup(self):
        try:
            import gi
            gi.require_version("Gtk", "4.0")
            gi.require_version("Adw", "1")
            from bbport_launcher import LauncherApp
            app = LauncherApp()
            app.register()
            self.assertTrue(app.get_is_registered())
        except Exception as e:
            self.fail(f"LauncherApp registration/startup failed: {e}")


if __name__ == "__main__":
    unittest.main()
