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
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)

        toolbar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        toolbar.add_css_class("log-toolbar")

        self.log_status_badge = Gtk.Label(label=tr("Игра не запущена"))
        self.log_status_badge.add_css_class("badge")
        self.log_status_badge.add_css_class("badge-warning")
        toolbar.append(self.log_status_badge)

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

    def on_clear_log(self, _button):
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
        buffer = self.log_view.get_buffer()
        buffer.insert(buffer.get_end_iter(), text)
        extra = buffer.get_line_count() - MAX_LOG_LINES
        if extra > 0:
            buffer.delete(buffer.get_start_iter(), buffer.get_iter_at_line(extra)[1])
        if getattr(self, "autoscroll_check", None) is None or self.autoscroll_check.get_active():
            adj = self.log_scroller.get_vadjustment()
            GLib.idle_add(lambda: adj.set_value(adj.get_upper()) and False)
