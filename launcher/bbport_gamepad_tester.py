# SPDX-License-Identifier: GPL-2.0-or-later
"""Interactive Real-Time Gamepad Tester Dialog for BBPort.

Visualizes controller connection state, hotplugging, buttons, D-Pad,
and analog axes (sticks and triggers) in real time using SDL3.
"""

import ctypes
import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, Gdk, GLib, Gtk, Pango

import os
from bbport_i18n import tr
from bbport_gamepad import (
    _SDL3,
    get_gamepad_type_label,
    load_custom_gamepad_mappings,
    BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH,
    BTN_BACK, BTN_GUIDE, BTN_START,
    BTN_LEFT_STICK, BTN_RIGHT_STICK,
    BTN_LEFT_SHOULDER, BTN_RIGHT_SHOULDER,
    BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT,
    AXIS_LEFTX, AXIS_LEFTY, AXIS_RIGHTX, AXIS_RIGHTY,
    AXIS_LEFT_TRIGGER, AXIS_RIGHT_TRIGGER,
)

BUTTON_DEFS = [
    (BTN_SOUTH, "A / ✕"),
    (BTN_EAST, "B / ○"),
    (BTN_WEST, "X / □"),
    (BTN_NORTH, "Y / △"),
    (BTN_LEFT_SHOULDER, "LB / L1"),
    (BTN_RIGHT_SHOULDER, "RB / R1"),
    (BTN_BACK, "Back / Share"),
    (BTN_START, "Start / Options"),
    (BTN_GUIDE, "Guide / Home"),
    (BTN_LEFT_STICK, "L3 (Click)"),
    (BTN_RIGHT_STICK, "R3 (Click)"),
    (BTN_DPAD_UP, "D-Pad ↑"),
    (BTN_DPAD_DOWN, "D-Pad ↓"),
    (BTN_DPAD_LEFT, "D-Pad ←"),
    (BTN_DPAD_RIGHT, "D-Pad →"),
]

_CSS_SETUP_DONE = False

def _setup_tester_css():
    global _CSS_SETUP_DONE
    if _CSS_SETUP_DONE:
        return
    provider = Gtk.CssProvider()
    provider.load_from_string("""
    .pad-chip {
        background-color: alpha(@window_fg_color, 0.08);
        border: 1px solid alpha(@window_fg_color, 0.18);
        border-radius: 8px;
        padding: 8px 12px;
        color: @window_fg_color;
        font-size: 13px;
        font-weight: 500;
        transition: all 60ms ease-out;
    }
    .pad-chip-active {
        background-color: #2ec27e;
        border-color: #26a269;
        color: #ffffff;
        font-weight: bold;
        box-shadow: 0 0 12px rgba(46, 194, 126, 0.6);
    }
    """)
    display = Gdk.Display.get_default()
    if display:
        Gtk.StyleContext.add_provider_for_display(
            display, provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )
    _CSS_SETUP_DONE = True


class GamepadTesterDialog(Adw.Window):
    """Real-time controller tester dialog with live axis and button visualization."""

    def __init__(self, parent_window=None):
        super().__init__()
        self.set_title(tr("Тестирование геймпада"))
        self.set_default_size(540, 640)
        self.set_modal(True)
        if parent_window:
            self.set_transient_for(parent_window)

        self.pad_handle = None
        self.pad_name = ""
        self.poll_timer_id = None

        _setup_tester_css()
        self._build_ui()
        self.connect("close-request", self.on_close)

        # Initialize SDL Gamepad subsystem if available
        if _SDL3:
            try:
                _SDL3.SDL_Init(0x00002000)  # SDL_INIT_GAMEPAD
                load_custom_gamepad_mappings()
            except Exception:
                pass
            self.poll_timer_id = GLib.timeout_add(16, self._on_poll)

    def _build_ui(self):
        root_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        header = Adw.HeaderBar()
        root_box.append(header)

        scrolled = Gtk.ScrolledWindow()
        scrolled.set_vexpand(True)
        root_box.append(scrolled)

        clamp = Adw.Clamp()
        clamp.set_maximum_size(500)
        clamp.set_tightening_threshold(400)
        scrolled.set_child(clamp)

        content = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=16)
        content.set_margin_top(16)
        content.set_margin_bottom(24)
        content.set_margin_start(16)
        content.set_margin_end(16)
        clamp.set_child(content)

        # Device status banner
        self.status_group = Adw.PreferencesGroup()
        self.device_row = Adw.ActionRow()
        self.device_row.set_title(tr("Геймпад не обнаружен"))
        self.device_row.set_subtitle(tr("Подключите геймпад для проверки кнопок и стиков"))
        self.status_icon = Gtk.Image.new_from_icon_name("input-gaming-symbolic")
        self.status_icon.set_pixel_size(32)
        self.device_row.add_prefix(self.status_icon)
        self.status_group.add(self.device_row)
        content.append(self.status_group)

        # Analog Axes Group
        axes_group = Adw.PreferencesGroup(title=tr("Аналоговые оси"))
        self.axis_widgets = {}

        axis_specs = [
            (AXIS_LEFTX, tr("Левый стик (X)"), True),
            (AXIS_LEFTY, tr("Левый стик (Y)"), True),
            (AXIS_RIGHTX, tr("Правый стик (X)"), True),
            (AXIS_RIGHTY, tr("Правый стик (Y)"), True),
            (AXIS_LEFT_TRIGGER, tr("Левый триггер (L2)"), False),
            (AXIS_RIGHT_TRIGGER, tr("Правый триггер (R2)"), False),
        ]

        for axis_id, label_text, is_bipolar in axis_specs:
            row = Adw.ActionRow(title=label_text)
            bar = Gtk.ProgressBar()
            bar.set_size_request(140, 14)
            bar.set_valign(Gtk.Align.CENTER)
            val_label = Gtk.Label(label="0.00" if is_bipolar else "0%")
            val_label.set_size_request(55, -1)
            val_label.set_xalign(1.0)
            val_label.set_valign(Gtk.Align.CENTER)
            val_label.add_css_class("dim-label")

            row.add_suffix(bar)
            row.add_suffix(val_label)
            axes_group.add(row)
            self.axis_widgets[axis_id] = (bar, val_label, is_bipolar)

        content.append(axes_group)

        # Buttons Group
        buttons_group = Adw.PreferencesGroup(title=tr("Кнопки"))

        self.active_row = Adw.ActionRow(title=tr("Нажатые кнопки:"))
        self.active_row.set_subtitle(tr("Нажмите любую кнопку на геймпаде"))
        buttons_group.add(self.active_row)

        btn_flow = Gtk.FlowBox()
        btn_flow.set_selection_mode(Gtk.SelectionMode.NONE)
        btn_flow.set_max_children_per_line(3)
        btn_flow.set_min_children_per_line(2)
        btn_flow.set_column_spacing(8)
        btn_flow.set_row_spacing(8)
        btn_flow.set_homogeneous(True)

        self.btn_widgets = {}
        for btn_id, btn_label in BUTTON_DEFS:
            label = Gtk.Label(label=f"○  {btn_label}")
            label.set_size_request(130, 38)
            label.add_css_class("pad-chip")
            btn_flow.append(label)
            self.btn_widgets[btn_id] = (label, btn_label)

        buttons_group.add(btn_flow)
        content.append(buttons_group)

        self.set_content(root_box)

    def _ensure_pad(self):
        if not _SDL3:
            return False

        if self.pad_handle:
            if _SDL3.SDL_GamepadConnected(self.pad_handle):
                return True
            try:
                _SDL3.SDL_CloseGamepad(self.pad_handle)
            except Exception:
                pass
            self.pad_handle = None
            self.pad_name = ""
            self.device_row.set_title(tr("Геймпад не обнаружен"))
            self.device_row.set_subtitle(tr("Подключите геймпад для проверки кнопок и стиков"))
            self._reset_ui_values()

        count = ctypes.c_int(0)
        gamepads = _SDL3.SDL_GetGamepads(ctypes.byref(count))
        if count.value > 0 and gamepads:
            self.pad_handle = _SDL3.SDL_OpenGamepad(gamepads[0])
            if self.pad_handle:
                name_bytes = _SDL3.SDL_GetGamepadName(self.pad_handle)
                self.pad_name = name_bytes.decode("utf-8", "replace") if name_bytes else "Gamepad"
                type_label = get_gamepad_type_label(self.pad_handle)
                self.device_row.set_title(f"{self.pad_name} ({type_label})")
                is_steam = any(k in os.environ for k in ("STEAM_COMPAT_CLIENT_INSTALL_PATH", "SteamTenfoot", "SteamGamepad", "SteamAppId"))
                if is_steam:
                    self.device_row.set_subtitle(tr("Steam Input ativo: a Steam gerencia este controle"))
                else:
                    self.device_row.set_subtitle(tr("Контроллер подключён и отвечает (SDL3)"))
                return True
        return False

    def _reset_ui_values(self):
        for bar, val_label, is_bipolar in self.axis_widgets.values():
            bar.set_fraction(0.5 if is_bipolar else 0.0)
            val_label.set_text("0.00" if is_bipolar else "0%")
        for widget, base_name in self.btn_widgets.values():
            widget.remove_css_class("pad-chip-active")
            widget.set_label(f"○  {base_name}")
        if hasattr(self, "active_row"):
            self.active_row.set_subtitle(tr("Нажмите любую кнопку на геймпаде"))

    def _on_poll(self):
        if not self.get_visible():
            return True

        if not self._ensure_pad():
            return True

        try:
            _SDL3.SDL_PumpEvents()

            # Poll axes
            for axis_id, (bar, val_label, is_bipolar) in self.axis_widgets.items():
                raw_val = _SDL3.SDL_GetGamepadAxis(self.pad_handle, axis_id)
                if is_bipolar:
                    fraction = (raw_val + 32768.0) / 65535.0
                    normalized = raw_val / 32767.0
                    bar.set_fraction(max(0.0, min(1.0, fraction)))
                    val_label.set_text(f"{normalized:+.2f}")
                else:
                    fraction = max(0.0, min(1.0, raw_val / 32767.0))
                    bar.set_fraction(fraction)
                    val_label.set_text(f"{int(fraction * 100)}%")

            # Poll buttons
            pressed_names = []
            for btn_id, (widget, base_name) in self.btn_widgets.items():
                is_pressed = _SDL3.SDL_GetGamepadButton(self.pad_handle, btn_id)
                if is_pressed:
                    pressed_names.append(base_name)
                    if not widget.has_css_class("pad-chip-active"):
                        widget.add_css_class("pad-chip-active")
                        widget.set_label(f"●  {base_name}")
                else:
                    if widget.has_css_class("pad-chip-active"):
                        widget.remove_css_class("pad-chip-active")
                        widget.set_label(f"○  {base_name}")

            if pressed_names:
                self.active_row.set_subtitle("  •  ".join(pressed_names))
            else:
                self.active_row.set_subtitle(tr("Нажмите любую кнопку на геймпаде"))
        except Exception:
            pass

        return True

    def on_close(self, _window):
        if self.poll_timer_id:
            GLib.source_remove(self.poll_timer_id)
            self.poll_timer_id = None
        if self.pad_handle and _SDL3:
            try:
                _SDL3.SDL_CloseGamepad(self.pad_handle)
            except Exception:
                pass
            self.pad_handle = None
        return False
