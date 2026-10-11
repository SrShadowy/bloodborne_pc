# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests for BBPort launcher gamepad navigation."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT / "launcher") not in sys.path:
    sys.path.insert(0, str(ROOT / "launcher"))

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, Gtk

from bbport_gamepad import (
    _load_sdl3,
    _is_descendant,
    _get_interactive_targets,
    _find_active_target,
    GamepadNavigator,
)


class TestGamepadNavigation(unittest.TestCase):
    def setUp(self):
        Adw.init()

    def test_sdl3_loader(self):
        # Must execute without throwing
        lib = _load_sdl3()
        # lib may be None or CDLL, but must not crash
        self.assertTrue(lib is None or hasattr(lib, "SDL_Init"))

    def test_is_descendant(self):
        box = Gtk.Box()
        btn = Gtk.Button()
        box.append(btn)
        self.assertTrue(_is_descendant(btn, box))
        self.assertFalse(_is_descendant(box, btn))

    def test_get_interactive_targets(self):
        root = Gtk.Box()
        group = Adw.PreferencesGroup()
        r1 = Adw.ActionRow(title="Row 1")
        r2 = Adw.SwitchRow(title="Row 2")
        r3 = Adw.SpinRow.new_with_range(0, 10, 1)
        r4 = Adw.ComboRow(title="Row 4")
        btn = Gtk.Button(label="Standalone")
        tab_btn = Gtk.Button(label="Tab")
        tab_btn.add_css_class("tab-bar-button")

        group.add(r1)
        group.add(r2)
        group.add(r3)
        group.add(r4)
        root.append(group)
        root.append(btn)
        root.append(tab_btn)

        targets = _get_interactive_targets(root)
        self.assertIn(r1, targets)
        self.assertIn(r2, targets)
        self.assertIn(r3, targets)
        self.assertIn(r4, targets)
        self.assertIn(btn, targets)
        self.assertNotIn(tab_btn, targets)

    def test_expander_targets(self):
        root = Gtk.Box()
        exp = Adw.ExpanderRow(title="Expander")
        sub_row = Adw.ActionRow(title="Sub")
        exp.add_row(sub_row)
        root.append(exp)

        # When collapsed
        exp.set_expanded(False)
        targets = _get_interactive_targets(root)
        self.assertIn(exp, targets)
        self.assertNotIn(sub_row, targets)

        # When expanded
        exp.set_expanded(True)
        targets = _get_interactive_targets(root)
        self.assertIn(exp, targets)
        self.assertIn(sub_row, targets)

    def test_popover_discovery_and_selection(self):
        win = Adw.ApplicationWindow()
        page = Adw.PreferencesPage()
        group = Adw.PreferencesGroup()
        row = Adw.ComboRow(title="Options")
        model = Gtk.StringList.new(["Alpha", "Beta", "Gamma"])
        row.set_model(model)
        row.set_selected(0)
        group.add(row)
        page.add(group)
        win.set_content(page)
        win.present()

        nav = GamepadNavigator(win)
        self.assertIsNone(nav._get_active_popover())

        # Open popover
        row.activate()
        pop = nav._get_active_popover()
        self.assertIsNotNone(pop)

        items = nav._get_popover_items(pop)
        self.assertEqual(len(items), 3)

        c_row = nav._get_popover_combo_row(pop)
        self.assertEqual(c_row, row)

        # Change selected and popdown
        c_row.set_selected(2)
        pop.popdown()
        self.assertEqual(row.get_selected(), 2)
        win.close()
        nav.close()

    def test_dialog_discovery_and_targets(self):
        win = Adw.ApplicationWindow()
        win.present()
        nav = GamepadNavigator(win)

        self.assertIsNone(nav._get_active_dialog())

        if hasattr(Adw, "AlertDialog"):
            dialog = Adw.AlertDialog(heading="Confirm", body="Do something?")
            dialog.add_response("cancel", "Cancel")
            dialog.add_response("ok", "OK")
            dialog.present(win)

            active_d = nav._get_active_dialog()
            self.assertEqual(active_d, dialog)

            targets = nav._get_dialog_targets(active_d)
            labels = [t.get_label() for t in targets]
            self.assertIn("Cancel", labels)
            self.assertIn("OK", labels)

            dialog.force_close()

        win.close()
        nav.close()

    def test_game_running_and_inactive_suspends_gamepad(self):
        win = Adw.ApplicationWindow()
        win.present()
        nav = GamepadNavigator(win)

        # Pretend a game process is running
        win.process = object()
        nav.prev_buttons[0] = True
        nav.repeat_dir = "down"

        # Poll should return True without crashing and reset button tracking states
        res = nav._on_poll()
        self.assertTrue(res)
        self.assertFalse(nav.prev_buttons[0])
        self.assertIsNone(nav.repeat_dir)

        # Clear process, test window inactive
        win.process = None
        win.is_active = lambda: False
        nav.prev_buttons[6] = True  # Start button

        res = nav._on_poll()
        self.assertTrue(res)
        self.assertFalse(nav.prev_buttons[6])

        win.close()
        nav.close()


if __name__ == "__main__":
    unittest.main()
