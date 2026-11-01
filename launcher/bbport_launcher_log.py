# SPDX-FileCopyrightText: Copyright 2026 bbport contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The launcher's log page: the game's output, search/filtering, copy and export."""

import time
from pathlib import Path
from bbport_config import MAX_LOG_LINES
from bbport_launcher_common import *  # noqa: F401,F403

import gi
gi.require_version("Gdk", "4.0")
from gi.repository import Gdk, GLib, Gtk  # noqa: E402


class LogPage:
    """Part of LauncherWindow (bbport_launcher.py)."""

    def build_log_page(self):
        if not hasattr(self, "_all_log_lines"):
            self._all_log_lines = []
        if not hasattr(self, "_log_filter_query"):
            self._log_filter_query = ""

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.log_view = Gtk.TextView(editable=False, monospace=True, cursor_visible=False,
                                     wrap_mode=Gtk.WrapMode.WORD_CHAR)
        self.log_view.set_top_margin(8)
        self.log_view.set_left_margin(8)
        self.log_view.set_right_margin(8)
        scroller = Gtk.ScrolledWindow(vexpand=True, child=self.log_view)
        self.log_scroller = scroller
        box.append(scroller)

        bar = Gtk.ActionBar()

        # Search toggle button (magnifying glass)
        self.log_search_toggle = Gtk.ToggleButton(icon_name="system-search-symbolic")
        self.log_search_toggle.add_css_class("flat")
        self.log_search_toggle.set_tooltip_text(tr("Поиск по журналу"))
        bar.pack_start(self.log_search_toggle)

        # Search text entry inside a revealer that slides in/out smoothly
        self.log_search_entry = Gtk.SearchEntry()
        self.log_search_entry.set_placeholder_text(tr("Фильтр (например: gpu)"))
        self.log_search_entry.set_max_width_chars(28)

        self.log_search_revealer = Gtk.Revealer(
            transition_type=Gtk.RevealerTransitionType.SLIDE_RIGHT,
            reveal_child=bool(self._log_filter_query)
        )
        self.log_search_revealer.set_child(self.log_search_entry)
        bar.pack_start(self.log_search_revealer)

        if self._log_filter_query:
            self.log_search_toggle.set_active(True)
            self.log_search_entry.set_text(self._log_filter_query)

        def _on_search_toggled(button):
            revealed = button.get_active()
            self.log_search_revealer.set_reveal_child(revealed)
            if revealed:
                self.log_search_entry.grab_focus()
            else:
                self.log_search_entry.set_text("")
                self._log_filter_query = ""
                self._refresh_log_buffer()

        self.log_search_toggle.connect("toggled", _on_search_toggled)

        def _on_search_changed(entry):
            self._log_filter_query = entry.get_text().strip().lower()
            self._refresh_log_buffer()

        self.log_search_entry.connect("search-changed", _on_search_changed)
        self.log_search_entry.connect("stop-search", lambda _: self.log_search_toggle.set_active(False))

        # Clear log button
        self.clear_log_button = Gtk.Button(icon_name="edit-clear-symbolic")
        self.clear_log_button.add_css_class("flat")
        self.clear_log_button.set_tooltip_text(tr("Очистить"))
        self.clear_log_button.connect("clicked", self.on_clear_log)
        bar.pack_start(self.clear_log_button)

        # Right side: Export and Copy
        self.export_log_button = Gtk.Button(label=tr("Экспорт журнала…"))
        self.export_log_button.connect("clicked", self.on_export_log)
        bar.pack_end(self.export_log_button)
        self.copy_log_button = Gtk.Button(label=tr("Копировать"))
        self.copy_log_button.connect("clicked", self.on_copy_log)
        bar.pack_end(self.copy_log_button)
        box.append(bar)

        # Keyboard shortcut: Ctrl+F toggles search
        key_ctrl = Gtk.EventControllerKey()
        def _on_key_pressed(_ctrl, keyval, _keycode, state):
            if (state & Gdk.ModifierType.CONTROL_MASK) and keyval in (Gdk.KEY_f, Gdk.KEY_F):
                self.log_search_toggle.set_active(not self.log_search_toggle.get_active())
                return True
            return False
        key_ctrl.connect("key-pressed", _on_key_pressed)
        box.add_controller(key_ctrl)

        buffer = self.log_view.get_buffer()
        buffer.connect("changed", self.on_log_changed)
        self.on_log_changed(buffer)
        return box

    def _refresh_log_buffer(self):
        if not hasattr(self, "log_view"):
            return
        buffer = self.log_view.get_buffer()
        lines = getattr(self, "_all_log_lines", [])
        query = getattr(self, "_log_filter_query", "")
        if query:
            matched = [line for line in lines if query in line.lower()]
            buffer.set_text("".join(matched))
        else:
            buffer.set_text("".join(lines))
        adj = self.log_scroller.get_vadjustment()
        GLib.idle_add(lambda: adj.set_value(adj.get_upper()) and False)

    def on_log_changed(self, buffer):
        has_text = buffer.get_char_count() > 0
        if hasattr(self, "copy_log_button"):
            self.copy_log_button.set_sensitive(has_text)
        if hasattr(self, "export_log_button"):
            self.export_log_button.set_sensitive(has_text)

    def log_text(self):
        buffer = self.log_view.get_buffer()
        return buffer.get_text(*buffer.get_bounds(), False)

    def on_copy_log(self, _button):
        self.get_clipboard().set(self.log_text())
        self.toasts.add_toast(Adw.Toast(title=tr("Журнал скопирован")))

    def on_clear_log(self, _button=None):
        if hasattr(self, "_all_log_lines"):
            self._all_log_lines.clear()
        if hasattr(self, "log_view"):
            self.log_view.get_buffer().set_text("")

    def on_export_log(self, _button):
        text = self.log_text()
        dialog = Gtk.FileDialog(title=tr("Экспорт журнала"))
        dialog.set_initial_name(f"bbport-{time.strftime('%Y%m%d_%H%M%S')}.log")

        def finish(dialog, result):
            try:
                chosen = dialog.save_finish(result)
            except GLib.Error:
                return
            if not chosen or not chosen.get_path():
                return
            try:
                Path(chosen.get_path()).write_text(text, encoding="utf-8")
            except OSError as error:
                self.toasts.add_toast(Adw.Toast(
                    title=tr("Не удалось сохранить журнал: {}").format(error.strerror or error)))
                return
            self.toasts.add_toast(Adw.Toast(title=tr("Журнал сохранён: {}").format(chosen.get_path())))
        dialog.save(self, None, finish)

    def append_log(self, text):
        if not hasattr(self, "_all_log_lines"):
            self._all_log_lines = []
        if self._all_log_lines and not self._all_log_lines[-1].endswith("\n"):
            lines = (self._all_log_lines[-1] + text).splitlines(keepends=True)
            self._all_log_lines.pop()
        else:
            lines = text.splitlines(keepends=True)
            if not lines and text:
                lines = [text]
        self._all_log_lines.extend(lines)
        extra_all = len(self._all_log_lines) - MAX_LOG_LINES
        if extra_all > 0:
            self._all_log_lines = self._all_log_lines[extra_all:]

        query = getattr(self, "_log_filter_query", "")
        if query:
            matching = [line for line in lines if query in line.lower()]
            if not matching:
                return
            to_insert = "".join(matching)
        else:
            to_insert = text

        buffer = self.log_view.get_buffer()
        buffer.insert(buffer.get_end_iter(), to_insert)
        extra = buffer.get_line_count() - MAX_LOG_LINES
        if extra > 0:
            buffer.delete(buffer.get_start_iter(), buffer.get_iter_at_line(extra)[1])
        adj = self.log_scroller.get_vadjustment()
        GLib.idle_add(lambda: adj.set_value(adj.get_upper()) and False)
