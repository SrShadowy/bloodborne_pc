"""Unit tests for the Desenvolvimento (Development) tab and log level filtering."""

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

from bbport_config import LOG_LEVELS, DEFAULTS, game_environment  # noqa: E402
from bbport_game_runner import GameRunnerMixin  # noqa: E402
from bbport_ui_helpers import combo_value  # noqa: E402


class MockRunner(GameRunnerMixin):
    def __init__(self, log_level="verbose"):
        self.settings = {"log_level": log_level}
        self._startup_done = False
        self.logged = []

    def append_log(self, text):
        self.logged.append(text)


class LauncherDevTests(unittest.TestCase):
    def test_log_levels_defined(self):
        self.assertIn("log_level", DEFAULTS)
        self.assertEqual(DEFAULTS["log_level"], "verbose")
        level_keys = [val for _label, val in LOG_LEVELS]
        self.assertIn("minimal", level_keys)
        self.assertIn("error", level_keys)
        self.assertIn("warning", level_keys)
        self.assertIn("verbose", level_keys)
        self.assertIn("none", level_keys)

    def test_game_environment_log_level(self):
        base_settings = dict(DEFAULTS)
        base_settings["game_dir"] = "/tmp"

        # Verbose
        base_settings["log_level"] = "verbose"
        env = game_environment(base_settings)
        self.assertEqual(env["BB_LOG_LEVEL"], "verbose")
        self.assertEqual(env["BB_GPU_LOG"], "debug")

        # Warning
        base_settings["log_level"] = "warning"
        env = game_environment(base_settings)
        self.assertEqual(env["BB_LOG_LEVEL"], "warning")
        self.assertEqual(env["BB_GPU_LOG"], "warning")

        # Error
        base_settings["log_level"] = "error"
        env = game_environment(base_settings)
        self.assertEqual(env["BB_LOG_LEVEL"], "error")
        self.assertEqual(env["BB_GPU_LOG"], "error")

        # Minimal
        base_settings["log_level"] = "minimal"
        env = game_environment(base_settings)
        self.assertEqual(env["BB_LOG_LEVEL"], "minimal")
        self.assertEqual(env["BB_GPU_LOG"], "error")

        # None (silent)
        base_settings["log_level"] = "none"
        env = game_environment(base_settings)
        self.assertEqual(env["BB_LOG_LEVEL"], "none")
        self.assertEqual(env["BB_GPU_LOG"], "off")

    def test_filter_mode_verbose(self):
        runner = MockRunner(log_level="verbose")
        self.assertTrue(runner._should_log_line("gpu info: device ready"))
        self.assertTrue(runner._should_log_line("random tick message"))
        self.assertTrue(runner._should_log_line("gpu warning: frame drop"))
        self.assertTrue(runner._should_log_line("fatal error: crashed"))

    def test_filter_mode_warning(self):
        runner = MockRunner(log_level="warning")
        self.assertFalse(runner._should_log_line("gpu info: device ready"))
        self.assertFalse(runner._should_log_line("audio tick 42"))
        self.assertTrue(runner._should_log_line("gpu warning: buffer full"))
        self.assertTrue(runner._should_log_line("error: out of memory"))
        self.assertTrue(runner._should_log_line("fatal: SIGSEGV"))

    def test_filter_mode_error(self):
        runner = MockRunner(log_level="error")
        self.assertFalse(runner._should_log_line("gpu info: device ready"))
        self.assertFalse(runner._should_log_line("gpu warning: buffer full"))
        self.assertFalse(runner._should_log_line("gpu error (Lib.NpManager): (STUBBED) called"))
        self.assertFalse(runner._should_log_line("gpu error (Lib.Net): (DUMMY) called"))
        self.assertTrue(runner._should_log_line("error: out of memory"))
        self.assertTrue(runner._should_log_line("fatal: SIGSEGV"))
        self.assertTrue(runner._should_log_line("Assertion failed: ptr != nullptr"))

    def test_filter_mode_none(self):
        runner = MockRunner(log_level="none")
        self.assertFalse(runner._should_log_line("Video backend: wayland"))
        self.assertFalse(runner._should_log_line("gpu warning: buffer full"))
        self.assertFalse(runner._should_log_line("error: generic error"))
        # Only true crashes / panics
        self.assertTrue(runner._should_log_line("panic: unhandled exception"))
        self.assertTrue(runner._should_log_line("Process crashed with SIGSEGV"))
        self.assertTrue(runner._should_log_line("Core dumped"))

    def test_filter_mode_minimal(self):
        runner = MockRunner(log_level="minimal")
        # Startup milestone lines pass
        self.assertTrue(runner._should_log_line("Video backend: x11 (XWayland on a Wayland session: wayland)"))
        self.assertTrue(runner._should_log_line("Bloodborne™ | entry=0xa0 | image=91,042,036 bytes"))
        self.assertTrue(runner._should_log_line("Memory model: new (AMD Radeon RX 9070 XT)"))
        self.assertTrue(runner._should_log_line("Co-op: summon signs through http://play.seamlesshunt.com"))
        self.assertTrue(runner._should_log_line("GPU: AMD Radeon RX 9070 XT"))
        self.assertTrue(runner._should_log_line("Upscaler: FSR 3.1 available"))

        # Internal runtime lines MUST be suppressed
        self.assertFalse(runner._should_log_line("Runtime: application heap API registered"))
        self.assertFalse(runner._should_log_line("Runtime: direct allocation offset=0x0"))
        self.assertFalse(runner._should_log_line("Runtime: mapped direct memory 0x1000000000"))
        self.assertFalse(runner._should_log_line("Runtime: semaphore id=1, attr=2, initial=0, max=1"))
        self.assertFalse(runner._should_log_line("Runtime: guest thread 'CSClothThread5' created"))
        self.assertFalse(runner._should_log_line("Runtime: save data 'SPRJ0005' mounted at /savedata0"))
        self.assertFalse(runner._should_log_line("Guest hooks: 2 of 2 resource copy sites"))
        self.assertFalse(runner._should_log_line("Guest memory: the GPU uses direct memory"))
        self.assertFalse(runner._should_log_line("gpu error (Lib.NpManager): (STUBBED) called"))

        # Transition to active gameplay
        self.assertTrue(runner._should_log_line("VideoOut: vblank 60 Hz, frame limit 0 FPS"))
        self.assertTrue(runner._startup_done)

        # Post-startup noisy gameplay messages are suppressed
        self.assertFalse(runner._should_log_line("ReceiveThread: unexpected SHAD packet"))
        self.assertFalse(runner._should_log_line("audio tick 100"))
        # But errors and crashes are still captured
        self.assertTrue(runner._should_log_line("error: failed to load texture"))
        self.assertTrue(runner._should_log_line("panic: memory corruption"))


class LauncherDevUITests(unittest.TestCase):
    def test_build_dev_page(self):
        from bbport_launcher import LauncherWindow
        win = MockRunner(log_level="minimal")
        win.settings = dict(DEFAULTS)
        win.settings["log_level"] = "minimal"
        win.build_dev_page = LauncherWindow.build_dev_page.__get__(win, MockRunner)
        win.store = lambda: None

        page = win.build_dev_page()
        self.assertIsInstance(page, Adw.PreferencesPage)
        self.assertIsNotNone(win.log_level_row)
        self.assertEqual(combo_value(win.log_level_row), "minimal")

        # Developer rows moved from performance page
        self.assertIsNotNone(win.mangohud_row)
        self.assertIsNotNone(win.stats_row)
        self.assertIsNotNone(win.save_log_row)
        self.assertIsNotNone(win.crash_diag_row)
        self.assertIsNotNone(win.as_0_3_row)
        self.assertIsNotNone(win.profile_row)
        self.assertIsNotNone(win.validation_row)
        self.assertIsNotNone(win.extra_row)

        # Test selecting "none"
        none_idx = next(i for i, (_label, val) in enumerate(LOG_LEVELS) if val == "none")
        win.log_level_row.set_selected(none_idx)
        self.assertEqual(combo_value(win.log_level_row), "none")

    def test_combo_row_tooltips_and_no_ellipsize(self):
        from bbport_ui_helpers import combo_row
        from gi.repository import Pango
        choices = [
            ("shadp2p / Windows BBSC (Gen 19)", "19", "Seamless Hunt compatibility"),
            ("shadPS4 0.20 (Gen 20)", "20", "Stack 0.20"),
        ]
        row = combo_row("Network Module", "Select module", choices, "19")
        self.assertIn("Seamless Hunt compatibility", row.get_subtitle())
        self.assertIn("Network Module", row.get_tooltip_text())
        self.assertIn("shadp2p / Windows BBSC (Gen 19)", row.get_tooltip_text())
        self.assertIn("Seamless Hunt compatibility", row.get_tooltip_text())

        # Test popover list factory behavior
        win = Adw.ApplicationWindow(default_width=600, default_height=400)
        box = Gtk.ListBox()
        box.append(row)
        win.set_content(box)

        # Find popover
        def find_popover(w):
            if isinstance(w, Gtk.Popover): return w
            c = w.get_first_child()
            while c:
                p = find_popover(c)
                if p: return p
                c = c.get_next_sibling()
            return None

        pop = find_popover(row)
        self.assertIsNotNone(pop)
        pop.popup()

        def find_labels(w):
            res = []
            if isinstance(w, Gtk.Label): res.append(w)
            c = w.get_first_child()
            while c:
                res.extend(find_labels(c))
                c = c.get_next_sibling()
            return res

        labels = [l for l in find_labels(pop) if l.get_text() == "shadp2p / Windows BBSC (Gen 19)"]
        self.assertTrue(len(labels) >= 1)
        target_label = labels[0]
        # Must not be ellipsized in popover
        self.assertEqual(target_label.get_ellipsize(), Pango.EllipsizeMode.NONE)
        # Must have full tooltip
        self.assertIn("shadp2p / Windows BBSC (Gen 19)", target_label.get_tooltip_text())
        self.assertIn("Seamless Hunt compatibility", target_label.get_tooltip_text())

    def test_dynamic_combo_row_setup(self):
        from bbport_ui_helpers import setup_combo_row
        from gi.repository import Pango
        row = setup_combo_row(Adw.ComboRow(title="Placa de vídeo"))
        choices = [
            "Automático (AMD Radeon RX 9070 XT)",
            "AMD Radeon RX 9070 XT",
            "AMD Ryzen 9 7900 12-Core Processor (integrada)"
        ]
        row.set_model(Gtk.StringList.new(choices))
        self.assertEqual(row.get_tooltip_text(), "Placa de vídeo\n\nAutomático (AMD Radeon RX 9070 XT)")

        win = Adw.ApplicationWindow(default_width=600, default_height=400)
        box = Gtk.ListBox()
        box.append(row)
        win.set_content(box)

        def find_popover(w):
            if isinstance(w, Gtk.Popover): return w
            c = w.get_first_child()
            while c:
                p = find_popover(c)
                if p: return p
                c = c.get_next_sibling()
            return None

        pop = find_popover(row)
        self.assertIsNotNone(pop)
        pop.popup()

        def find_labels(w):
            res = []
            if isinstance(w, Gtk.Label): res.append(w)
            c = w.get_first_child()
            while c:
                res.extend(find_labels(c))
                c = c.get_next_sibling()
            return res

        for choice_str in choices:
            matching = [l for l in find_labels(pop) if l.get_text() == choice_str]
            self.assertTrue(len(matching) >= 1)
            self.assertEqual(matching[0].get_ellipsize(), Pango.EllipsizeMode.NONE)
            self.assertEqual(matching[0].get_tooltip_text(), choice_str)



if __name__ == "__main__":
    unittest.main()
