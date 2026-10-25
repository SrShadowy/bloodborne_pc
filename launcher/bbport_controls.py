# SPDX-License-Identifier: GPL-2.0-or-later
"""Controls settings, mouse configuration, and key/gamepad bindings for BBPort."""

from bbport_config import PORT_DIR, PACKAGED, CONTROLS
from bbport_devices import format_mouse_btn, connected_gamepads, connected_displays
from bbport_i18n import tr
from bbport_ui_helpers import flat_button, setup_combo_row

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, Gio, GLib, Gtk  # noqa: E402


class ControlsMixin:
    """Mixin for LauncherWindow handling controls, keybindings, mouse and gamepads."""

    def build_controls_page(self):
        page = Adw.PreferencesPage()

        # --- Keyboard & Mouse (Combined) -------------------------------------------------
        kbm_group = Adw.PreferencesGroup(
            title=tr("Клавиатура и мышь"),
            description=tr("Управление камерой аналоговым движением мыши, как правым стиком. Нажмите иконку клавиатуры или мыши для назначения."))

        self.mouse_sens_row = Adw.SpinRow.new_with_range(0.1, 5.0, 0.1)
        self.mouse_sens_row.set_title(tr("Чувствительность камеры (мышь)"))
        self.mouse_sens_row.set_subtitle(tr("Скорость поворота камеры аналоговым движением мыши"))
        try:
            cur_sens = float(self.ini.get("mouse_sensitivity", "1.00") or 1.0)
        except (ValueError, TypeError):
            cur_sens = 1.0
        self.mouse_sens_row.set_value(cur_sens)
        self.mouse_sens_row.connect("notify::value", lambda *_: self.on_mouse_setting_changed())
        kbm_group.add(self.mouse_sens_row)

        self.mouse_inv_y_row = Adw.SwitchRow(title=tr("Инвертировать по вертикали (Y)"),
                                             active=self.ini.get("mouse_invert_y") == "1")
        self.mouse_inv_y_row.connect("notify::active", lambda *_: self.on_mouse_setting_changed())
        kbm_group.add(self.mouse_inv_y_row)

        self.mouse_inv_x_row = Adw.SwitchRow(title=tr("Инвертировать по горизонтали (X)"),
                                             active=self.ini.get("mouse_invert_x") == "1")
        self.mouse_inv_x_row.connect("notify::active", lambda *_: self.on_mouse_setting_changed())
        kbm_group.add(self.mouse_inv_x_row)

        self.mouse_capture_row = Adw.SwitchRow(
            title=tr("Захват курсора в игре"),
            subtitle=tr("Блокирует курсор в окне во время игры; Insert или F10 освобождают"),
            active=self.ini.get("mouse_capture", "1") != "0")
        self.mouse_capture_row.connect("notify::active", lambda *_: self.on_mouse_setting_changed())
        kbm_group.add(self.mouse_capture_row)

        self.kbm_expander = Adw.ExpanderRow(
            title=tr("Назначение клавиш и мыши"),
            subtitle=tr("Клавиатура и мышь объединены; применяется при запуске игры"))
        kbm_expander = self.kbm_expander
        self.kbm_rows = {}
        self.control_rows = {}
        for name, label, key_default, pad_default, mouse_default in CONTROLS:
            if key_default is None and mouse_default is None:
                continue
            row = Adw.ActionRow(title=tr(label))
            row.add_suffix(flat_button("input-keyboard-symbolic", tr("Назначить клавишу"),
                                       lambda _b, n=name: self.assign_control("key", n)))
            if not name.startswith("move_"):
                row.add_suffix(flat_button("input-mouse-symbolic", tr("Назначить кнопку мыши"),
                                           lambda _b, n=name: self.assign_control("mouse", n)))
            row.add_suffix(flat_button("edit-undo-symbolic", tr("Сбросить"),
                                       lambda _b, n=name: self.reset_kbm_control(n)))
            kbm_expander.add_row(row)
            self.kbm_rows[name] = (row, key_default, mouse_default)
            self.control_rows[("key", name)] = (row, key_default)
            self.control_rows[("mouse", name)] = (row, mouse_default)
            self.show_kbm_control(name)
        kbm_group.add(kbm_expander)
        page.add(kbm_group)

        # --- Gamepad ---------------------------------------------------------------------
        gamepad_group = Adw.PreferencesGroup(title=tr("Геймпад"))
        self.gamepad_row = setup_combo_row(Adw.ComboRow(title=tr("Контроллер")))
        self.gamepad_row.connect("notify::selected", lambda *_: self.show_gamepad())
        self.gamepad_row.add_suffix(flat_button("view-refresh-symbolic", tr("Обновить список"),
                                                lambda _button: self.fill_gamepads()))
        self.fill_gamepads()
        gamepad_group.add(self.gamepad_row)

        test_row = Adw.ActionRow(title=tr("Проверить геймпад"),
                                 subtitle=tr("Тестирование кнопок, стиков и триггеров в реальном времени"))
        test_btn = flat_button("input-gaming-symbolic", tr("Проверить"),
                               lambda _button: self.on_test_gamepad())
        test_row.add_suffix(test_btn)
        test_row.set_activatable_widget(test_btn)
        gamepad_group.add(test_row)

        self.disable_gyro_row = Adw.SwitchRow(
            title=tr("Отключить гироскоп"),
            subtitle=tr("Игнорировать датчики движения (жесты SixAxis)"),
            active=self.ini.get("disable_gyro") == "1")
        self.disable_gyro_row.connect("notify::active", lambda *_: self.on_gamepad_setting_changed())
        gamepad_group.add(self.disable_gyro_row)

        self.pad_expander = Adw.ExpanderRow(
            title=tr("Назначение кнопок геймпада"),
            subtitle=tr("Применяется при запуске игры"))
        pad_expander = self.pad_expander
        self.pad_rows = {}
        for name, label, key_default, pad_default, mouse_default in CONTROLS:
            if pad_default is None:
                continue
            row = Adw.ActionRow(title=tr(label))
            row.add_suffix(flat_button("input-gaming-symbolic", tr("Назначить кнопку"),
                                       lambda _b, n=name: self.assign_control("pad", n)))
            row.add_suffix(flat_button("edit-undo-symbolic", tr("Сбросить"),
                                       lambda _b, n=name: self.set_control("pad", n, None)))
            pad_expander.add_row(row)
            self.pad_rows[name] = (row, pad_default)
            self.control_rows[("pad", name)] = (row, pad_default)
            self.show_pad_control(name)
        gamepad_group.add(pad_expander)
        page.add(gamepad_group)

        return page

    def show_kbm_control(self, name):
        if not hasattr(self, "kbm_rows") or name not in self.kbm_rows:
            return
        row, key_default, mouse_default = self.kbm_rows[name]
        key_val = self.ini.get(f"key.{name}")
        mouse_val = self.ini.get(f"mouse.{name}")

        key_str = key_val if key_val is not None else key_default
        mouse_str = mouse_val if mouse_val is not None else mouse_default

        key_display = key_str if key_str else tr("не назначено")
        mouse_display = format_mouse_btn(mouse_str) if mouse_str else tr("не назначено")

        if name.startswith("move_"):
            row.set_subtitle(tr("Клавиатура: {}").format(key_display))
        else:
            row.set_subtitle(tr("Клавиатура: {} • Мышь: {}").format(key_display, mouse_display))

    def show_pad_control(self, name):
        if not hasattr(self, "pad_rows") or name not in self.pad_rows:
            return
        row, pad_default = self.pad_rows[name]
        val = self.ini.get(f"pad.{name}")
        if val is None:
            row.set_subtitle(tr("{} (по умолчанию)").format(pad_default) if pad_default else tr("не назначено"))
        else:
            row.set_subtitle(val or tr("не назначено"))

    def show_control(self, kind, name):
        if kind in ("key", "mouse"):
            self.show_kbm_control(name)
        elif kind == "pad":
            self.show_pad_control(name)
        elif hasattr(self, "control_rows") and (kind, name) in self.control_rows:
            row, default = self.control_rows[(kind, name)]
            value = self.ini.get(f"{kind}.{name}")
            if value is None:
                row.set_subtitle(tr("{} (по умолчанию)").format(default) if default else tr("не назначено"))
            else:
                row.set_subtitle(value or tr("не назначено"))

    def reset_kbm_control(self, name):
        self.ini[f"key.{name}"] = None
        self.ini[f"mouse.{name}"] = None
        self.show_kbm_control(name)
        self.store()

    def set_control(self, kind, name, value):
        """value: the binding, or None for the default."""
        self.ini[f"{kind}.{name}"] = value
        self.show_control(kind, name)
        self.store()

    def assign_control(self, kind, name):
        tool = PORT_DIR / ("bin" if PACKAGED else "out") / "bb-gpu-capabilities"
        try:
            process = Gio.Subprocess.new([str(tool), "--read-input", kind],
                                         Gio.SubprocessFlags.STDOUT_PIPE)
        except GLib.Error as error:
            self.toasts.add_toast(Adw.Toast(title=tr("Не удалось запустить: {}").format(error.message)))
            return
        if kind in ("key", "mouse") and hasattr(self, "kbm_rows") and name in self.kbm_rows:
            row = self.kbm_rows[name][0]
        elif kind == "pad" and hasattr(self, "pad_rows") and name in self.pad_rows:
            row = self.pad_rows[name][0]
        elif hasattr(self, "control_rows") and (kind, name) in self.control_rows:
            row, _default = self.control_rows[(kind, name)]
        else:
            return

        self.input_assignment_active = True

        if kind == "mouse":
            prompt = tr("Кликните кнопкой мыши… (Esc — отмена)")
        elif kind == "key":
            prompt = tr("Нажмите клавишу на клавиатуре… (Esc — отмена)")
        else:
            prompt = tr("Нажмите кнопку на геймпаде… (Esc — отмена)")
        row.set_subtitle(prompt)

        def done(proc, result):
            self.input_assignment_active = False
            try:
                _ok, out, _err = proc.communicate_utf8_finish(result)
            except GLib.Error:
                out = ""
            words = (out or "").strip().split(" ", 1)
            if len(words) == 2 and words[0] == kind:
                self.set_control(kind, name, words[1])
            else:
                self.show_control(kind, name)
        process.communicate_utf8_async(None, None, done)

    def on_mouse_setting_changed(self):
        if hasattr(self, "mouse_sens_row"):
            self.ini["mouse_sensitivity"] = f"{self.mouse_sens_row.get_value():.2f}"
            self.ini["mouse_invert_y"] = "1" if self.mouse_inv_y_row.get_active() else "0"
            self.ini["mouse_invert_x"] = "1" if self.mouse_inv_x_row.get_active() else "0"
            self.ini["mouse_capture"] = "1" if self.mouse_capture_row.get_active() else "0"
            self.store()

    def on_gamepad_setting_changed(self):
        if hasattr(self, "disable_gyro_row"):
            self.ini["disable_gyro"] = "1" if self.disable_gyro_row.get_active() else "0"
            self.store()

    def fill_gamepads(self):
        """The controller choices: the first connected one, the connected ones, and the saved
        choice while it is not connected."""
        current = self.settings.get("gamepad", "")
        choices = [(tr("Авто"), "", tr("Первый подключённый"))]
        choices += [(name, guid, name) for guid, name in connected_gamepads()]
        if current and current not in [guid for _, guid, _ in choices]:
            name = self.settings.get("gamepad_name") or current
            choices.append((tr("{} (не подключён)").format(name), current, name))
        self.gamepad_row.set_model(Gtk.StringList.new([label for label, _, _ in choices]))
        self.gamepad_row.values = [guid for _, guid, _ in choices]
        self.gamepad_row.names = [name for _, _, name in choices]
        self.gamepad_row.set_selected(self.gamepad_row.values.index(current) if current in self.gamepad_row.values else 0)
        self.show_gamepad()

    def fill_displays(self):
        """The monitor choices: the primary one, the connected ones, and the saved choice while it
        is not connected."""
        displays = connected_displays()
        self.display_row.displays = displays
        if displays:
            choices = [(value, label) for value, label, _, _ in displays]
            primaries = [value for value, _, _, primary in displays if primary]
            default = primaries[0] if primaries else choices[0][0]
        else:
            choices = [("", tr("Основной"))]
            default = ""
        current = self.settings.get("display", "")
        self.display_row.set_model(Gtk.StringList.new([label for _, label in choices]))
        self.display_row.values = [value for value, _ in choices]
        selected = self.display_row.values.index(current) if current in self.display_row.values else (
            self.display_row.values.index(default) if default in self.display_row.values else 0
        )
        self.display_row.set_selected(selected)

    def show_gamepad(self):
        names = getattr(self.gamepad_row, "names", None)
        idx = self.gamepad_row.get_selected()
        selected = names[idx] if names and 0 <= idx < len(names) else ""
        self.gamepad_row.set_subtitle("\n".join(
            line for line in (selected, tr("Выбранный берётся, как только подключится")) if line))

    def on_test_gamepad(self):
        try:
            from bbport_gamepad_tester import GamepadTesterDialog
            dialog = GamepadTesterDialog(parent_window=self)
            dialog.present()
        except Exception as e:
            self.toasts.add_toast(Adw.Toast(title=tr("Не удалось открыть тест геймпада: {}").format(e)))
