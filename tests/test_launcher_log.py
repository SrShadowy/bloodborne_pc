"""Unit tests for the launcher log viewer and filtering."""

import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
from paths import ROOT

sys.path.insert(0, str(ROOT / "launcher"))

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, Gtk  # noqa: E402

from bbport_log import LogMixin  # noqa: E402


class MockLauncherWindow(LogMixin):
    def __init__(self):
        self.toasts = Adw.ToastOverlay()
        self.stack = Gtk.Stack()


class LauncherLogFilterTests(unittest.TestCase):
    def setUp(self):
        self.win = MockLauncherWindow()
        self.page = self.win.build_log_page()

    def test_log_widgets_created(self):
        self.assertIsNotNone(self.win.log_status_box)
        self.assertIsNotNone(self.win.log_status_icon)
        self.assertIsNotNone(self.win.log_status_badge)
        self.assertIsNotNone(self.win.log_search_toggle)
        self.assertIsNotNone(self.win.log_search_entry)
        self.assertIsNotNone(self.win.log_search_revealer)
        self.assertFalse(self.win.log_search_toggle.get_active())
        self.assertFalse(self.win.log_search_revealer.get_reveal_child())

    def test_append_and_filter_log(self):
        self.win.append_log("runtime: starting up\n")
        self.win.append_log("gpu: Vulkan device initialized\n")
        self.win.append_log("audio: SDL stream opened\n")
        self.win.append_log("gpu warning: shader cache miss\n")

        full = self.win.log_text()
        self.assertIn("runtime: starting up", full)
        self.assertIn("gpu: Vulkan device initialized", full)
        self.assertIn("audio: SDL stream opened", full)
        self.assertIn("gpu warning: shader cache miss", full)

        # Activate filter "gpu"
        self.win.log_search_entry.set_text("gpu")
        self.win._log_filter_query = "gpu"
        self.win._refresh_log_buffer()

        filtered = self.win.log_text()
        self.assertNotIn("runtime: starting up", filtered)
        self.assertNotIn("audio: SDL stream opened", filtered)
        self.assertIn("gpu: Vulkan device initialized", filtered)
        self.assertIn("gpu warning: shader cache miss", filtered)

        # Append while filter is active
        self.win.append_log("gpu: pipeline compiled\n")
        self.win.append_log("net: room member joined\n")

        filtered2 = self.win.log_text()
        self.assertIn("gpu: pipeline compiled", filtered2)
        self.assertNotIn("net: room member joined", filtered2)

        # Clear filter
        self.win.log_search_entry.set_text("")
        self.win._log_filter_query = ""
        self.win._refresh_log_buffer()

        restored = self.win.log_text()
        self.assertIn("runtime: starting up", restored)
        self.assertIn("audio: SDL stream opened", restored)
        self.assertIn("net: room member joined", restored)
        self.assertIn("gpu: pipeline compiled", restored)

    def test_clear_log(self):
        self.win.append_log("sample log entry\n")
        self.assertIn("sample log entry", self.win.log_text())
        self.win.on_clear_log()
        self.assertEqual(self.win.log_text(), "")
        self.assertEqual(self.win._all_log_lines, [])


if __name__ == "__main__":
    unittest.main()
