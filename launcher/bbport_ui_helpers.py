# SPDX-License-Identifier: GPL-2.0-or-later
"""Reusable GTK4 and Libadwaita UI components and widget helpers for BBPort."""

from pathlib import Path
from bbport_i18n import tr

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, Gio, GLib, Gtk, Pango  # noqa: E402


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


def setup_combo_row(row, tooltips=None):
    """Configures an Adw.ComboRow with tooltips and non-ellipsized items in popover and row."""
    if tooltips is not None:
        row.tooltips = tooltips

    def update_row_tooltip(*_):
        title = row.get_title() or ""
        sub = row.get_subtitle() or ""
        idx = row.get_selected()
        item_tip = None
        if hasattr(row, "tooltips") and 0 <= idx < len(row.tooltips):
            item_tip = row.tooltips[idx]
        elif hasattr(row, "get_model") and row.get_model():
            m = row.get_model()
            if 0 <= idx < m.get_n_items():
                item_obj = m.get_item(idx)
                if hasattr(item_obj, "get_string"):
                    item_tip = item_obj.get_string()
        tip_parts = [p for p in (title, sub, item_tip) if p]
        if tip_parts:
            row.set_tooltip_text("\n\n".join(dict.fromkeys(tip_parts)))
        else:
            row.set_tooltip_text(None)

    row.connect("notify::selected", update_row_tooltip)
    row.connect("notify::subtitle", update_row_tooltip)
    row.connect("notify::model", update_row_tooltip)

    factory = row.get_factory()
    if factory and not getattr(row, "_factory_bound", False):
        row._factory_bound = True

        def on_bind(_factory, item):
            obj = item.get_item()
            pos = item.get_position()
            text = obj.get_string() if (obj and hasattr(obj, "get_string")) else ""
            tip = None
            if hasattr(row, "tooltips") and 0 <= pos < len(row.tooltips):
                tip = row.tooltips[pos]
            elif hasattr(row, "get_item_tooltip") and callable(row.get_item_tooltip):
                tip = row.get_item_tooltip(pos, text)
            else:
                tip = text or None

            box = item.get_child()
            if box:
                in_popover = box.get_ancestor(Gtk.Popover) is not None
                c = box.get_first_child()
                while c:
                    if isinstance(c, Gtk.Label):
                        if in_popover:
                            c.set_ellipsize(Pango.EllipsizeMode.NONE)
                        else:
                            c.set_ellipsize(Pango.EllipsizeMode.END)
                        if tip:
                            c.set_tooltip_text(tip)
                    elif isinstance(c, Gtk.Image):
                        if tip:
                            c.set_tooltip_text(tip)
                    c = c.get_next_sibling()
                if tip:
                    box.set_tooltip_text(tip)

        factory.connect("bind", on_bind)

    update_row_tooltip()
    return row


def combo_row(title, subtitle, choices, current):
    """Creates an Adw.ComboRow initialized with choices and localized notes."""
    model = Gtk.StringList.new([tr(choice[0]) for choice in choices])
    row = Adw.ComboRow(title=title, model=model)
    values = [choice[1] for choice in choices]
    notes = [tr(choice[2]) if len(choice) > 2 and choice[2] else None for choice in choices]
    tooltips = [
        f"{tr(choice[0])}\n\n{notes[i]}" if notes[i] else tr(choice[0])
        for i, choice in enumerate(choices)
    ]

    def show_note(*_):
        idx = row.get_selected()
        current_note = notes[idx] if 0 <= idx < len(notes) else None
        lines = [line for line in (subtitle, current_note) if line]
        row.set_subtitle("\n".join(lines))

    setup_combo_row(row, tooltips=tooltips)
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


def set_combo_value(row, value):
    """Sets the selected item in an Adw.ComboRow created with combo_row by value."""
    if hasattr(row, "values") and value in row.values:
        row.set_selected(row.values.index(value))


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
