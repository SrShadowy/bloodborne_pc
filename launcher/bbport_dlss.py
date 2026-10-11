# SPDX-License-Identifier: GPL-2.0-or-later
"""NVIDIA DLSS runtime library discovery, selection, and installation for BBPort."""

import shutil
from pathlib import Path
from bbport_config import NGX_PREFIX, DLSS_SDK_URL, binaries_dir, ngx_libraries
from bbport_i18n import tr

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, Gio, GLib, Gtk  # noqa: E402


class DlssMixin:
    """Mixin for LauncherWindow handling NVIDIA DLSS libraries."""

    def update_dlss_row(self):
        own = ngx_libraries(self.user_dir() / "dlss")
        bundled = ngx_libraries(binaries_dir())
        bridge = (binaries_dir() / "libbbport_dlss.so").is_file()
        if own:
            text = tr("Своя: {}").format(own[-1].name[len(NGX_PREFIX):])
        elif bundled:
            text = tr("Из сборки: {}").format(bundled[-1].name[len(NGX_PREFIX):])
        else:
            text = tr("Нет: выберите libnvidia-ngx-dlss.so.* из DLSS SDK NVIDIA ({})").format(DLSS_SDK_URL)
        if not bridge:
            text += "\n" + tr("Эта сборка без моста DLSS (libbbport_dlss.so): DLSS работать не будет")
        self.dlss_row.set_subtitle(text)
        self.dlss_reset_button.set_sensitive(bool(own))
        if hasattr(self, "sharpness_row"):  # built after this row
            self.update_upscaler_status()

    def choose_ngx(self):
        dialog = Gtk.FileDialog(title=tr("Библиотека DLSS NVIDIA (libnvidia-ngx-dlss.so.*)"))
        files = Gtk.FileFilter()
        files.set_name("libnvidia-ngx-dlss.so.*, nvngx_dlss.dll")
        for pattern in ("libnvidia-ngx-dlss.so*", "*.dll", "*.DLL"):
            files.add_pattern(pattern)
        filters = Gio.ListStore.new(Gtk.FileFilter)
        filters.append(files)
        dialog.set_filters(filters)
        dialog.set_default_filter(files)

        def finish(dialog, result):
            try:
                chosen = dialog.open_finish(result)
            except GLib.Error:
                return
            if chosen and chosen.get_path():
                self.install_ngx(Path(chosen.get_path()))
        dialog.open(self, None, finish)

    def install_ngx(self, path):
        if path.suffix.lower() == ".dll":
            self.alert(tr("Нужна библиотека для Linux"), tr(
                "{} — библиотека DLSS для Windows; на Linux NVIDIA её не загружает. Нужна "
                "libnvidia-ngx-dlss.so.<версия> из DLSS SDK NVIDIA: {}").format(path.name, DLSS_SDK_URL))
            return
        try:
            with open(path, "rb") as f:
                elf = f.read(4) == b"\x7fELF"
        except OSError as error:
            self.alert(tr("Не удалось прочитать файл"), str(error))
            return
        if not path.name.startswith(NGX_PREFIX) or not elf:
            self.alert(tr("Это не библиотека DLSS"), tr(
                "Нужен файл libnvidia-ngx-dlss.so.<версия> из DLSS SDK NVIDIA: {}").format(DLSS_SDK_URL))
            return
        target = self.user_dir() / "dlss"
        try:
            target.mkdir(parents=True, exist_ok=True)
            for old in ngx_libraries(target):
                old.unlink()
            shutil.copy2(path, target / path.name)
        except OSError as error:
            self.alert(tr("Не удалось скопировать библиотеку"), str(error))
            return
        self.toasts.add_toast(Adw.Toast(title=tr("DLSS {}: готово").format(path.name[len(NGX_PREFIX):])))
        self.update_dlss_row()

    def reset_ngx(self):
        for old in ngx_libraries(self.user_dir() / "dlss"):
            try:
                old.unlink()
            except OSError:
                pass
        self.update_dlss_row()
