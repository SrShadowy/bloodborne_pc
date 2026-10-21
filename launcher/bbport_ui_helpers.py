# SPDX-License-Identifier: GPL-2.0-or-later
"""Reusable GTK4 and Libadwaita UI components and widget helpers for BBPort."""

from pathlib import Path
from bbport_i18n import tr

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, Gio, GLib, Gtk  # noqa: E402


def open_url(url):
    """Opens url in the user's browser: GIO, then Python's webbrowser (xdg-open and the like);
    True when one of them took it."""
    try:
        if Gio.AppInfo.launch_default_for_uri(url, None):
            return True
    except GLib.Error:
        pass
    try:
        import webbrowser
        return bool(webbrowser.open(url))
    except Exception:
        return False



def flat_button(icon, tooltip, handler):
    """Creates a flat button with an icon and tooltip."""
    button = Gtk.Button(icon_name=icon, valign=Gtk.Align.CENTER, tooltip_text=tooltip)
    button.add_css_class("flat")
    button.connect("clicked", handler)
    return button


def open_folder(window, path):
    """Opens `path` in the desktop file manager (creating it if needed)."""
    if not path:
        return
    try:
        p = Path(path).expanduser().resolve()
        p.mkdir(parents=True, exist_ok=True)
    except OSError:
        return

    import os
    import shutil
    import subprocess

    # Sanitize AppImage / bundled library environment so host file managers (Dolphin, Nautilus, etc.)
    # don't crash from conflicting Qt / GLib / GTK libraries.
    clean_env = os.environ.copy()
    for var in ("LD_LIBRARY_PATH", "PYTHONPATH", "PYTHONHOME", "GIO_EXTRA_MODULES", "GI_TYPELIB_PATH"):
        clean_env.pop(var, None)

    for opener in ("xdg-open", "gio"):
        if shutil.which(opener):
            try:
                cmd = [opener, str(p)] if opener == "xdg-open" else [opener, "open", str(p)]
                subprocess.Popen(cmd, env=clean_env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                return
            except Exception:
                pass

    try:
        if Gio.AppInfo.launch_default_for_uri(p.as_uri(), None):
            return
    except Exception:
        pass

    try:
        Gtk.FileLauncher.new(Gio.File.new_for_path(str(p))).launch(window, None, None)
    except Exception:
        pass


def combo_row(title, subtitle, choices, current):
    """Creates an Adw.ComboRow initialized with choices and localized notes."""
    model = Gtk.StringList.new([tr(choice[0]) for choice in choices])
    row = Adw.ComboRow(title=title, model=model)
    values = [choice[1] for choice in choices]
    notes = [tr(choice[2]) if len(choice) > 2 else None for choice in choices]

    def show_note(*_):
        lines = [line for line in (subtitle, notes[row.get_selected()]) if line]
        row.set_subtitle("\n".join(lines))

    row.set_selected(values.index(current) if current in values else 0)
    show_note()
    row.connect("notify::selected", show_note)
    row.values = values
    return row


def combo_value(row):
    """Extracts the underlying value from an Adw.ComboRow created with combo_row."""
    if not hasattr(row, "values") or not row.values:
        return ""
    idx = row.get_selected()
    if 0 <= idx < len(row.values):
        return row.values[idx]
    return row.values[0] if row.values else ""


class FolderList:
    """A preferences group with a folder row and dynamic switch rows below it."""

    def __init__(self, group):
        self.group = group
        self.rows = []

    def clear(self):
        for _, row in self.rows:
            self.group.remove(row)
        self.rows = []

    def add(self, key, row):
        self.group.add(row)
        self.rows.append((key, row))
