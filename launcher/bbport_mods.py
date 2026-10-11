# SPDX-License-Identifier: GPL-2.0-or-later
"""Mods and third-party XML patches management for BBPort."""

import json
from pathlib import Path
from bbport_config import DATA_DIR, patches_dir
from bbport_i18n import tr
from bbport_ui_helpers import flat_button, open_folder, FolderList
from mods import discover as discover_mods
from patches import external_patches

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, GLib  # noqa: E402


class ModsMixin:
    """Mixin for LauncherWindow handling mods and patches."""

    def build_mods_page(self):
        page = Adw.PreferencesPage()

        self.mods_group = Adw.PreferencesGroup(
            title=tr("Моды"), description=tr(
                "Распакуйте каждый мод в отдельную папку (с dvdroot_ps4 или сразу с chr/, parts/ и т. п.). "
                "При совпадении файлов побеждает мод ниже в списке. Применяется при запуске."))
        self.mods_enabled_row = Adw.SwitchRow(title=tr("Загружать моды"),
                                               active=self.settings["mods_enabled"])
        self.mods_group.add(self.mods_enabled_row)
        self.mods_folder_row = Adw.ActionRow(title=tr("Папка модов"))
        self.mods_folder_row.add_suffix(flat_button("folder-open-symbolic", tr("Выбрать папку модов"),
                                                    self.on_choose_mods))
        self.mods_folder_row.add_suffix(flat_button("system-file-manager-symbolic", tr("Открыть папку модов"),
                                                    lambda _b: open_folder(self, self.mods_dir())))
        self.mods_folder_row.add_suffix(flat_button("view-refresh-symbolic", tr("Обновить список"),
                                                    self.on_refresh_mods))
        self.mods_group.add(self.mods_folder_row)
        self.mod_list = FolderList(self.mods_group)
        self.refresh_mods()
        page.add(self.mods_group)

        self.patches_group = Adw.PreferencesGroup(
            title=tr("Сторонние патчи"),
            description=tr("XML-патчи для версии 01.09 из папки патчей. "
                           "Применяются при запуске."))
        self.patches_folder_row = Adw.ActionRow(title=tr("Папка патчей"))
        self.patches_folder_row.add_suffix(flat_button("folder-open-symbolic", tr("Выбрать папку патчей"),
                                                       self.on_choose_patches))
        self.patches_folder_row.add_suffix(flat_button(
            "system-file-manager-symbolic", tr("Открыть папку патчей"),
            lambda _b: open_folder(self, patches_dir(self.settings))))
        self.patches_folder_row.add_suffix(flat_button("view-refresh-symbolic", tr("Обновить список"),
                                                       self.on_refresh_patches))
        self.patches_group.add(self.patches_folder_row)
        self.patch_list = FolderList(self.patches_group)
        self.refresh_patches()
        page.add(self.patches_group)

        return page

    def mods_dir(self):
        return Path(self.settings.get("mods_dir") or DATA_DIR / "mods").expanduser()

    def save_mod_profile(self):
        DATA_DIR.mkdir(parents=True, exist_ok=True)
        rows = self.mod_list.rows
        profile = {"order": [name for name, _ in rows],
                   "disabled": [name for name, row in rows if not row.get_active()]}
        (DATA_DIR / "mods.json").write_text(json.dumps(profile, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    def refresh_mods(self):
        self.mod_list.clear()
        self.mods_folder_row.set_subtitle(str(self.mods_dir()))
        try:
            profile = json.loads((DATA_DIR / "mods.json").read_text(encoding='utf-8'))
        except (OSError, ValueError):
            profile = {}
        available = discover_mods(self.mods_dir())
        order = list(dict.fromkeys(n for n in [*profile.get("order", []), *available] if n in available))
        for name in order:
            row = Adw.SwitchRow(title=name, active=name not in profile.get("disabled", []))
            row.add_suffix(flat_button("go-up-symbolic", tr("Загрузить раньше"),
                                        lambda _b, n=name: self.move_mod(n, -1)))
            row.add_suffix(flat_button("go-down-symbolic", tr("Загрузить позже"),
                                        lambda _b, n=name: self.move_mod(n, 1)))
            self.mod_list.add(name, row)
        if not order:
            self.mods_folder_row.set_subtitle(f"{self.mods_dir()} — {tr('Модов нет')}")

    def move_mod(self, name, direction):
        rows = self.mod_list.rows
        index = next(i for i, (n, _) in enumerate(rows) if n == name)
        target = index + direction
        if 0 <= target < len(rows):
            rows[index], rows[target] = rows[target], rows[index]
            self.save_mod_profile()
            self.refresh_mods()

    def on_refresh_mods(self, _button):
        self.save_mod_profile()
        self.refresh_mods()

    def on_choose_mods(self, _button):
        self.save_mod_profile()

        def chosen(path):
            self.settings["mods_dir"] = path
            self.refresh_mods()
            self.store()
        self.choose_folder(tr("Папка модов"), self.mods_dir(), chosen)

    def save_patch_profile(self):
        """patches.json: the switches that differ from each file's isEnabled."""
        enabled, disabled = [], []
        for key, row in self.patch_list.rows:
            if row.get_active() != row.default:
                (enabled if row.get_active() else disabled).append(key)
        path = DATA_DIR / "patches.json"
        try:
            profile = json.loads(path.read_text(encoding='utf-8'))
        except (OSError, ValueError):
            profile = {}
        # Patches of files not listed now (another folder) keep their choice.
        shown = {key for key, _ in self.patch_list.rows}
        profile = {"enabled": sorted({k for k in profile.get("enabled", []) if k not in shown} | set(enabled)),
                   "disabled": sorted({k for k in profile.get("disabled", []) if k not in shown} | set(disabled))}
        DATA_DIR.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(profile, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    def refresh_patches(self):
        self.patch_list.clear()
        directory = patches_dir(self.settings)
        try:
            profile = json.loads((DATA_DIR / "patches.json").read_text(encoding='utf-8'))
        except (OSError, ValueError):
            profile = {}
        found = external_patches(directory)
        for key, _path, meta in found:
            default = meta.get("isEnabled", "false").lower() == "true"
            active = key in profile.get("enabled", []) or (default and key not in profile.get("disabled", []))
            subtitle = key.split("/", 1)[0]
            if meta.get("Author"):
                subtitle += " · " + tr("Автор: {}").format(meta.get("Author"))
            row = Adw.SwitchRow(title=GLib.markup_escape_text(meta.get("Name") or key),
                                subtitle=GLib.markup_escape_text(subtitle), active=active)
            if meta.get("Note"):
                row.set_tooltip_text(meta.get("Note").replace("\\n", "\n"))
            row.default = default
            self.patch_list.add(key, row)
        self.patches_folder_row.set_subtitle(
            str(directory) if found else f"{directory} — {tr('Патчей нет')}")

    def on_refresh_patches(self, _button):
        self.save_patch_profile()
        self.refresh_patches()

    def on_choose_patches(self, _button):
        self.save_patch_profile()

        def chosen(path):
            self.settings["patches_dir"] = path
            self.refresh_patches()
            self.store()
        self.choose_folder(tr("Папка патчей"), patches_dir(self.settings), chosen)
