# SPDX-License-Identifier: GPL-2.0-or-later
"""Log viewer page, terminal scrolling, clipboard copy, and export for BBPort."""

import time
from pathlib import Path
from bbport_config import DATA_DIR, MAX_LOG_LINES
from bbport_i18n import tr
from bbport_ui_helpers import open_folder

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, GLib, Gtk  # noqa: E402


class LogMixin:
    """Mixin for LauncherWindow handling the log page, terminal output, and export."""

    def build_log_page(self):
        if not hasattr(self, "_all_log_lines"):
            self._all_log_lines = []
        if not hasattr(self, "_log_filter_query"):
            self._log_filter_query = ""

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)

        toolbar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        toolbar.add_css_class("log-toolbar")

        status_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
        status_box.add_css_class("badge")
        status_box.add_css_class("badge-warning")
        self.log_status_box = status_box

        self.log_status_icon = Gtk.Image.new_from_icon_name("media-playback-stop-symbolic")
        status_box.append(self.log_status_icon)

        self.log_status_badge = Gtk.Label(label=tr("Игра не запущена"))
        status_box.append(self.log_status_badge)
        toolbar.append(status_box)

        self.log_search_toggle = Gtk.ToggleButton(icon_name="system-search-symbolic")
        self.log_search_toggle.add_css_class("flat")
        self.log_search_toggle.set_tooltip_text(tr("Поиск по журналу"))
        toolbar.append(self.log_search_toggle)

        self.log_search_entry = Gtk.SearchEntry()
        self.log_search_entry.set_placeholder_text(tr("Фильтр (например: gpu)"))
        self.log_search_entry.set_max_width_chars(25)

        self.log_search_revealer = Gtk.Revealer(
            transition_type=Gtk.RevealerTransitionType.SLIDE_RIGHT,
            reveal_child=bool(self._log_filter_query)
        )
        self.log_search_revealer.set_child(self.log_search_entry)
        toolbar.append(self.log_search_revealer)

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

        spacer = Gtk.Box()
        spacer.set_hexpand(True)
        toolbar.append(spacer)

        copy_btn = Gtk.Button(label=tr("Скопировать"), icon_name="edit-copy-symbolic")
        copy_btn.add_css_class("flat")
        copy_btn.connect("clicked", self.on_copy_log)
        toolbar.append(copy_btn)

        clear_btn = Gtk.Button(label=tr("Очистить"), icon_name="edit-clear-symbolic")
        clear_btn.add_css_class("flat")
        clear_btn.connect("clicked", self.on_clear_log)
        toolbar.append(clear_btn)

        diag_btn = Gtk.Button(label=tr("Диагностика"), icon_name="dialog-information-symbolic")
        diag_btn.add_css_class("flat")
        diag_btn.connect("clicked", self.on_copy_diagnostics)
        toolbar.append(diag_btn)

        open_logs_btn = Gtk.Button(label=tr("Открыть папку журнала"), icon_name="folder-open-symbolic")
        open_logs_btn.add_css_class("flat")

        def _open_logs(_b):
            try:
                from diagnostics import get_log_dir
                target = get_log_dir() or (DATA_DIR / "logs")
            except Exception:
                target = DATA_DIR / "logs"
            open_folder(self, target)

        open_logs_btn.connect("clicked", _open_logs)
        toolbar.append(open_logs_btn)

        self.autoscroll_check = Gtk.CheckButton(label=tr("Автопрокрутка"), active=True)
        toolbar.append(self.autoscroll_check)

        box.append(toolbar)

        self.log_view = Gtk.TextView(editable=False, monospace=True, cursor_visible=False,
                                     wrap_mode=Gtk.WrapMode.WORD_CHAR)
        self.log_view.add_css_class("log-terminal")
        self.log_view.set_top_margin(10)
        self.log_view.set_bottom_margin(10)
        self.log_view.set_left_margin(12)
        self.log_view.set_right_margin(12)
        scroller = Gtk.ScrolledWindow(vexpand=True, child=self.log_view)
        self.log_scroller = scroller
        box.append(scroller)
        bar = Gtk.ActionBar()
        self.export_log_button = Gtk.Button(label=tr("Экспорт журнала…"))
        self.export_log_button.connect("clicked", self.on_export_log)
        bar.pack_end(self.export_log_button)
        self.copy_log_button = Gtk.Button(label=tr("Копировать"))
        self.copy_log_button.connect("clicked", self.on_copy_log)
        bar.pack_end(self.copy_log_button)
        box.append(bar)
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
        if getattr(self, "autoscroll_check", None) is None or self.autoscroll_check.get_active():
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

    def on_copy_diagnostics(self, _button):
        from diagnostics import collect_diagnostics_report
        report = collect_diagnostics_report()
        self.get_clipboard().set(report)
        self.toasts.add_toast(Adw.Toast(title=tr("Диагностика скопирована в буфер обмена")))

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
        if getattr(self, "autoscroll_check", None) is None or self.autoscroll_check.get_active():
            adj = self.log_scroller.get_vadjustment()
            GLib.idle_add(lambda: adj.set_value(adj.get_upper()) and False)
