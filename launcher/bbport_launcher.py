#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bloodborne launcher for the native port (GTK4 / libadwaita).

Picks the game folder, edits the port's settings (bbport.ini: upscaler, preset, ...) and the
start-up tweaks passed as environment variables to run.sh, starts and stops the game and shows
its output. Launcher settings live in ~/.config/bbport-launcher/settings.json.
Russian, English, Brazilian Portuguese and Simplified Chinese (bbport_i18n: the Russian
text is the key).
"""

import os
import faulthandler
import subprocess
import sys
import time
from pathlib import Path
from bbport_assets import fsr411_problem
from bbport_i18n import set_language, tr

# [Diagnostics & Logging Bootstrap — Phase 0]
_LAUNCHER_DIR = Path(__file__).resolve().parent
if str(_LAUNCHER_DIR) not in sys.path:
    sys.path.insert(0, str(_LAUNCHER_DIR))

from diagnostics import (
    LOG,
    setup_logging,
    get_log_dir,
    get_log_file,
    get_runtime_backend,
    collect_diagnostics_report,
)

_LOG_DIR = setup_logging()
_LAUNCHER_LOG = get_log_file() or (Path.home() / ".config" / "bbport-launcher" / "launcher.log")


def _launcher_log(line):
    LOG.info(line)


# Detect or configure launcher window backend (X11 vs Wayland).
# Respect CLI flags (--x11, --wayland) or BB_LAUNCHER_BACKEND env var. Default to modern OpenGL renderer.
if "--x11" in sys.argv:
    os.environ["GDK_BACKEND"] = "x11"
    os.environ.setdefault("GSK_RENDERER", "gl")
elif "--wayland" in sys.argv:
    os.environ["GDK_BACKEND"] = "wayland"
    os.environ.setdefault("GSK_RENDERER", "gl")
else:
    _launcher_backend = os.environ.get("BB_LAUNCHER_BACKEND", "").lower()
    if _launcher_backend == "x11":
        os.environ["GDK_BACKEND"] = "x11"
        os.environ.setdefault("GSK_RENDERER", "gl")
    elif _launcher_backend == "wayland":
        os.environ["GDK_BACKEND"] = "wayland"
        os.environ.setdefault("GSK_RENDERER", "gl")
    else:
        os.environ.setdefault("GSK_RENDERER", "gl")

if getattr(sys, "frozen", False):
    _meipass_typelibs = Path(getattr(sys, "_MEIPASS", "")) / "girepository-1.0"
    if _meipass_typelibs.is_dir():
        os.environ["GI_TYPELIB_PATH"] = str(_meipass_typelibs)

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
import bbport_compat  # noqa: E402
bbport_compat.apply_shims()
from gi.repository import Adw, Gdk, Gio, GLib, Gtk, Pango  # noqa: E402
if "BB_PORT_DIR" in os.environ:
    PORT_DIR = Path(os.environ["BB_PORT_DIR"]).resolve()
elif getattr(sys, "frozen", False):
    exe_path = Path(sys.executable).resolve()
    PORT_DIR = exe_path.parent.parent if exe_path.parent.name in ("launcher", "bin") else exe_path.parent
else:
    PORT_DIR = Path(__file__).resolve().parent.parent  # native_probe (or the package's copy)
sys.path.insert(0, str(PORT_DIR / 'scripts'))
sys.path.insert(0, str(PORT_DIR / 'tools'))
import game_check  # noqa: E402
import fsr4_wizard  # noqa: E402
from bbport_config import (
    PORT_DIR, PACKAGED, DATA_DIR, CONFIG_FILE, DEFAULTS, BB_VERSION,
    PC_MODEL_SUBTITLE, PACKAGE_ONLY_ENV,
    UI_LANGUAGES, UPSCALERS, PRESETS, OUTPUT_RES, EFFECTS, MODEL_LOD,
    FPS_MODES, PRESENT_MODES, DRAW_PIPE, LANGUAGES, LIVE_RESOLUTION,
    READBACKS, PREUPLOAD, VK_RECORD_THREADS, MEMORY_MODELS,
    ASPECTS, output_sizes, resolved_aspect, nearest_output, parse_size,
    NGX_PREFIX, DLSS_SDK_URL, binaries_dir, ngx_libraries, online_module, VIDEO_BACKENDS,
    load_settings, save_settings, load_ini, save_ini,
    game_environment,
)
from bbport_devices import connected_gpus
from bbport_fsr_builder import (
    FSR4CAP_DIR, FSR411_BUILD_ERRORS, Fsr411Manager, fsr411_dir, fsr411_build_command,
)
from bbport_ui_helpers import (
    flat_button, open_folder, combo_row, combo_value,
)
from bbport_cache import CacheMixin
from bbport_controls import ControlsMixin
from bbport_mods import ModsMixin
from bbport_online import OnlineMixin
from bbport_bbhost_ui import BbhostAccountMixin
from bbport_dlss import DlssMixin
from bbport_log import LogMixin
from bbport_game_runner import GameRunnerMixin
from bbport_gamepad import GamepadNavigator


def setup_icons():
    """Configures fallback icon search path and ensures symbolic icons are loaded."""
    display = Gdk.Display.get_default()
    if display:
        try:
            icon_theme = Gtk.IconTheme.get_for_display(display)
            icons_dir = Path(__file__).resolve().parent / "icons"
            if icons_dir.is_dir():
                icon_theme.add_search_path(str(icons_dir))

            # If current desktop icon theme lacks standard Adwaita symbolics (e.g. on KDE Breeze),
            # fall back to Adwaita icon theme
            settings = Gtk.Settings.get_default()
            if settings:
                current_theme = settings.get_property("gtk-icon-theme-name")
                if not current_theme or not icon_theme.has_icon("video-display-symbolic"):
                    settings.set_property("gtk-icon-theme-name", "Adwaita")
        except Exception as e:
            print(f"[!] Warning: Icon theme setup failed: {e}", file=sys.stderr)


_AdwWindowBase = Adw.ApplicationWindow if isinstance(Adw.ApplicationWindow, type) else object


class LauncherWindow(
    ControlsMixin,
    ModsMixin,
    OnlineMixin,
    BbhostAccountMixin,
    CacheMixin,
    DlssMixin,
    GameRunnerMixin,
    LogMixin,
    _AdwWindowBase,
):
    def __init__(self, app):
        setup_icons()
        super().__init__(application=app, title="Bloodborne")
        self.set_default_size(980, 780)
        self.settings = load_settings()
        self.theme_mode = self.settings.get("theme_mode", "dark")
        self.apply_theme(self.theme_mode)
        try:
            self.setup_css()
        except Exception as e:
            print(f"[!] Warning: CSS styling setup failed: {e}", file=sys.stderr)
        set_language(self.settings.get("ui_language", ""))
        self.ini, self.ini_lines = load_ini()
        self.process = None
        self.stream = None
        self.fsr411_mgr = Fsr411Manager(self)
        self.best_detected_dll = None
        self.gamepad_nav = None
        self.build()
        LOG.info("startup.sdl.begin")
        try:
            self.gamepad_nav = GamepadNavigator(self)
            if self.gamepad_nav and self.gamepad_nav.pad_handle:
                LOG.info("startup.sdl.ok: gamepad_connected=%s", self.gamepad_nav.pad_name)
            else:
                LOG.info("startup.sdl.ok: no gamepad currently connected")
        except Exception as e:
            LOG.warning("[W-SDL-001] startup.sdl.degraded: gamepad navigation unavailable (%s)", e)
            self.gamepad_nav = None
        self.connect("close-request", self.on_close)

    @property
    def fsr411_build(self):
        return self.fsr411_mgr.build_proc

    @fsr411_build.setter
    def fsr411_build(self, val):
        self.fsr411_mgr.build_proc = val

    def setup_css(self):
        css = """
        .tab-bar-button {
            padding: 7px 14px;
            font-weight: 600;
            font-size: 13px;
            color: #9aa2b1;
        }
        .tab-bar-button:hover {
            color: #ffffff;
            background-color: rgba(255, 255, 255, 0.08);
        }
        .tab-bar-button:checked {
            background-color: rgba(255, 255, 255, 0.16);
            color: #ffffff;
        }
        .log-toolbar {
            background-color: #161922;
            border-bottom: 1px solid rgba(255, 255, 255, 0.08);
            padding: 8px 12px;
            color: #c9d1d9;
        }
        .log-toolbar label {
            color: #c9d1d9;
        }
        .log-toolbar button {
            color: #c9d1d9;
        }
        .badge {
            border-radius: 6px;
            padding: 3px 8px;
            font-size: 11px;
            font-weight: 600;
        }
        .badge-warning {
            background-color: rgba(220, 100, 20, 0.2);
            color: #f59e0b;
            border: 1px solid rgba(245, 158, 11, 0.3);
        }
        .badge-success {
            background-color: rgba(34, 197, 94, 0.2);
            color: #22c55e;
            border: 1px solid rgba(34, 197, 94, 0.3);
        }
        .log-terminal {
            background-color: #0b0d13;
            color: #c9d1d9;
            font-family: monospace;
            font-size: 12px;
            line-height: 1.4;
        }
        button.suggested-action {
            background: linear-gradient(180deg, #b01e1e 0%, #8e1515 100%);
            color: #ffffff;
            border: 1px solid #731111;
        }
        button.suggested-action:hover {
            background: linear-gradient(180deg, #c42424 0%, #9e1717 100%);
        }
        button.suggested-action:active {
            background: #731111;
        }
        .gamepad-hints-bar {
            background-color: rgba(18, 21, 28, 0.95);
            border-top: 1px solid rgba(255, 255, 255, 0.08);
            padding: 5px 12px;
        }
        .gamepad-badge {
            background-color: rgba(255, 255, 255, 0.12);
            border: 1px solid rgba(255, 255, 255, 0.22);
            border-radius: 4px;
            padding: 1px 6px;
            font-weight: 700;
            font-size: 11px;
            color: #f0f0f0;
        }
        .gamepad-label {
            font-size: 11px;
            color: #9aa2b1;
            margin-right: 4px;
        }
        .gamepad-name {
            font-size: 11px;
            font-weight: 600;
            color: #c9d1d9;
            margin-right: 6px;
        }
        row:focus, row:focus-visible, button:focus, button:focus-visible {
            outline: 2px solid #e03131;
            outline-offset: -1px;
        }
        popover listview row:focus, popover listview row:focus-visible, popover listview row:selected {
            background-color: rgba(224, 49, 49, 0.35);
            outline: 2px solid #e03131;
            outline-offset: -1px;
        }
        adw-dialog-host button:focus, adw-dialog-host button:focus-visible {
            outline: 2px solid #e03131;
            outline-offset: -1px;
        }

        /* Light theme overrides */
        window.light-theme .tab-bar-button {
            color: #5c6574;
        }
        window.light-theme .tab-bar-button:hover {
            color: #111827;
            background-color: rgba(0, 0, 0, 0.06);
        }
        window.light-theme .tab-bar-button:checked {
            background-color: rgba(0, 0, 0, 0.12);
            color: #111827;
        }
        window.light-theme .log-toolbar {
            background-color: #f1f3f7;
            border-bottom: 1px solid rgba(0, 0, 0, 0.08);
            color: #1e293b;
        }
        window.light-theme .log-toolbar label {
            color: #1e293b;
        }
        window.light-theme .log-toolbar button {
            color: #1e293b;
        }
        window.light-theme .badge-warning {
            background-color: rgba(245, 158, 11, 0.15);
            color: #b45309;
            border: 1px solid rgba(245, 158, 11, 0.35);
        }
        window.light-theme .badge-success {
            background-color: rgba(34, 197, 94, 0.15);
            color: #15803d;
            border: 1px solid rgba(34, 197, 94, 0.35);
        }
        window.light-theme .log-terminal {
            background-color: #ffffff;
            color: #1e293b;
        }
        window.light-theme .gamepad-hints-bar {
            background-color: rgba(241, 243, 247, 0.95);
            border-top: 1px solid rgba(0, 0, 0, 0.08);
        }
        window.light-theme .gamepad-badge {
            background-color: rgba(0, 0, 0, 0.08);
            border: 1px solid rgba(0, 0, 0, 0.15);
            color: #1e293b;
        }
        window.light-theme .gamepad-label {
            color: #5c6574;
        }
        window.light-theme .gamepad-name {
            color: #1e293b;
        }
        """
        provider = Gtk.CssProvider()
        loaded = False
        if hasattr(provider, "load_from_string"):
            try:
                provider.load_from_string(css)
                loaded = True
            except Exception:
                pass
        if not loaded:
            for attempt in [
                lambda: provider.load_from_data(css.encode(), -1),
                lambda: provider.load_from_data(css.encode()),
                lambda: provider.load_from_data(css, -1),
                lambda: provider.load_from_data(css),
            ]:
                try:
                    attempt()
                    loaded = True
                    break
                except Exception:
                    continue
        display = Gdk.Display.get_default()
        if display:
            try:
                Gtk.StyleContext.add_provider_for_display(
                    display, provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
            except Exception:
                pass

    def set_gamepad_hints_visible(self, visible, pad_name=""):
        if hasattr(self, "gamepad_bar"):
            self.gamepad_bar.set_visible(visible)
            if hasattr(self, "gamepad_name_label") and pad_name:
                self.gamepad_name_label.set_label(pad_name)

    def update_theme_button(self):
        if not hasattr(self, "theme_button") or not self.theme_button:
            return
        if getattr(self, "theme_mode", "dark") == "light":
            self.theme_button.set_icon_name("weather-clear-night-symbolic")
            self.theme_button.set_tooltip_text(tr("Тёмная тема"))
        else:
            self.theme_button.set_icon_name("display-brightness-symbolic")
            self.theme_button.set_tooltip_text(tr("Светлая тема"))

    def apply_theme(self, mode=None, save=False):
        if mode is None:
            mode = self.settings.get("theme_mode", "dark")
        self.theme_mode = mode
        self.settings["theme_mode"] = mode
        if save:
            save_settings(self.settings)

        sm = Adw.StyleManager.get_default()
        if mode == "light":
            sm.set_color_scheme(Adw.ColorScheme.FORCE_LIGHT)
            self.remove_css_class("dark-theme")
            self.add_css_class("light-theme")
        else:
            sm.set_color_scheme(Adw.ColorScheme.FORCE_DARK)
            self.remove_css_class("light-theme")
            self.add_css_class("dark-theme")

        self.update_theme_button()

    def on_toggle_theme(self, _button):
        next_mode = "dark" if self.theme_mode == "light" else "light"
        self.apply_theme(next_mode, save=True)

    def build(self):
        """(Re)creates the window's content in the current launcher language."""
        toolbar = Adw.ToolbarView()
        header = Adw.HeaderBar()
        self.stack = Adw.ViewStack()

        title_widget = Adw.WindowTitle(title="Bloodborne", subtitle=f"BBPort {BB_VERSION}")
        header.set_title_widget(title_widget)

        self.theme_button = Gtk.Button()
        self.theme_button.add_css_class("flat")
        self.theme_button.connect("clicked", self.on_toggle_theme)
        header.pack_start(self.theme_button)

        self.launch_button = Gtk.Button()
        self.launch_button.connect("clicked", self.on_launch)
        header.pack_end(self.launch_button)
        toolbar.add_top_bar(header)

        self.toasts = Adw.ToastOverlay()
        self.toasts.set_child(self.stack)
        toolbar.set_content(self.toasts)

        # Gamepad hints bar (pinned at bottom, shown when gamepad connected)
        self.gamepad_bar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=14)
        self.gamepad_bar.add_css_class("gamepad-hints-bar")
        self.gamepad_bar.set_halign(Gtk.Align.CENTER)
        self.gamepad_bar.set_visible(False)

        pad_info = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        pad_icon = Gtk.Image.new_from_icon_name("input-gaming-symbolic")
        self.gamepad_name_label = Gtk.Label(label="Gamepad")
        self.gamepad_name_label.add_css_class("gamepad-name")
        pad_info.append(pad_icon)
        pad_info.append(self.gamepad_name_label)
        self.gamepad_bar.append(pad_info)

        def make_hint(keys, text):
            box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
            for k in keys:
                b = Gtk.Label(label=k)
                b.add_css_class("gamepad-badge")
                box.append(b)
            lbl = Gtk.Label(label=text)
            lbl.add_css_class("gamepad-label")
            box.append(lbl)
            return box

        self.gamepad_bar.append(make_hint(["LB", "RB"], tr("Вкладки")))
        self.gamepad_bar.append(make_hint(["D-Pad", "LS"], tr("Навигация")))
        self.gamepad_bar.append(make_hint(["A"], tr("Выбрать")))
        self.gamepad_bar.append(make_hint(["B"], tr("Назад")))
        self.gamepad_bar.append(make_hint(["Y"], tr("В фокус Играть")))
        self.gamepad_bar.append(make_hint(["Start"], tr("Запуск")))
        self.gamepad_bar.append(make_hint(["RS"], tr("Прокрутка")))

        toolbar.add_bottom_bar(self.gamepad_bar)
        self.set_content(toolbar)

        log_text = self.log_text() if hasattr(self, "log_view") else ""

        perf_icon = "speedometer-symbolic"
        try:
            display = Gdk.Display.get_default()
            if display:
                it = Gtk.IconTheme.get_for_display(display)
                if not it.has_icon("speedometer-symbolic") and it.has_icon("power-profile-performance-symbolic"):
                    perf_icon = "power-profile-performance-symbolic"
        except Exception:
            pass

        tab_items = [
            ("overview", tr("Главная"), "user-home-symbolic", self.build_overview_page),
            ("graphics", tr("Графика"), "video-display-symbolic", self.build_graphics_page),
            ("performance", tr("Производительность"), perf_icon, self.build_performance_page),
            ("controls", tr("Управление"), "input-gaming-symbolic", self.build_controls_page),
            ("online", tr("Онлайн"), "network-workgroup-symbolic", self.build_online_page),
            ("mods", tr("Моды и патчи"), "application-x-addon-symbolic", self.build_mods_page),
            ("log", tr("Журнал"), "utilities-terminal-symbolic", self.build_log_page),
        ]

        self.tab_buttons = {}
        tab_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=0)
        tab_box.add_css_class("linked")
        tab_box.set_halign(Gtk.Align.CENTER)

        group = None
        for name, title, icon, build_fn in tab_items:
            page_widget = build_fn()
            self.stack.add_titled_with_icon(page_widget, name, title, icon)

            btn = Gtk.ToggleButton()
            btn.add_css_class("tab-bar-button")
            if group is None:
                group = btn
            else:
                btn.set_group(group)

            btn_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
            img = Gtk.Image.new_from_icon_name(icon)
            lbl = Gtk.Label(label=title)
            lbl.set_ellipsize(Pango.EllipsizeMode.NONE)
            btn_box.append(img)
            btn_box.append(lbl)
            btn.set_child(btn_box)

            def on_tab_toggled(button, tab_name=name):
                if button.get_active():
                    self.stack.set_visible_child_name(tab_name)

            btn.connect("toggled", on_tab_toggled)
            tab_box.append(btn)
            self.tab_buttons[name] = btn

        if group is not None:
            group.set_active(True)

        def on_stack_visible_child_changed(_stack, _param):
            active_name = self.stack.get_visible_child_name()
            if active_name in self.tab_buttons:
                btn = self.tab_buttons[active_name]
                if not btn.get_active():
                    btn.set_active(True)

        self.stack.connect("notify::visible-child-name", on_stack_visible_child_changed)

        tab_scroll = Gtk.ScrolledWindow()
        tab_scroll.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.NEVER)
        tab_scroll.set_propagate_natural_width(True)
        tab_scroll.set_child(tab_box)
        tab_scroll.set_halign(Gtk.Align.CENTER)
        tab_scroll.set_hexpand(True)
        tab_scroll.set_margin_top(6)
        tab_scroll.set_margin_bottom(8)

        toolbar.add_top_bar(tab_scroll)
        self.log_view.get_buffer().set_text(log_text)
        self.update_launch_button()
        self.update_game_status()
        self.update_user_status()
        self.update_upscaler_status()
        self.update_fsr411_auto_row()
        self.update_theme_button()
        if hasattr(self, "gamepad_nav") and self.gamepad_nav and self.gamepad_nav.pad_handle:
            self.set_gamepad_hints_visible(True, self.gamepad_nav.pad_name)

    def update_launch_button(self):
        running = self.process is not None
        label = tr("Остановить") if running else tr("Запустить")
        self.launch_button.set_label(label)
        path = self.game_dir()
        ok = bool(self.settings["game_dir"]) and (path / "eboot.bin").is_file()
        can_launch = (ok or running) and (self.fsr411_build is None)
        self.launch_button.set_sensitive(can_launch)
        self.launch_button.remove_css_class("destructive-action" if not running else "suggested-action")
        self.launch_button.add_css_class("destructive-action" if running else "suggested-action")
        if hasattr(self, "fsr411_button"):
            self.fsr411_button.set_sensitive(not running and self.fsr411_build is None)
        if hasattr(self, "fsr411_auto_button"):
            self.fsr411_auto_button.set_sensitive(not running and self.fsr411_build is None and getattr(self, "best_detected_dll", None) is not None)
        if hasattr(self, "set_gamepad_hints_visible"):
            if running:
                self.set_gamepad_hints_visible(False)
            elif hasattr(self, "gamepad_nav") and self.gamepad_nav and self.gamepad_nav.pad_handle:
                self.set_gamepad_hints_visible(True, self.gamepad_nav.pad_name)

    def update_fsr411_auto_row(self):
        if not hasattr(self, "fsr411_auto_row"):
            return
        building = self.fsr411_build is not None
        best_dll, best_loader = fsr4_wizard.auto_select_best_dll()
        if best_dll:
            self.best_detected_dll = best_dll
            self.best_detected_loader = best_loader
            p_str = str(best_dll)
            src = "Goverlay" if "goverlay" in p_str else ("Steam" if "Steam" in p_str else "Sistema")
            self.fsr411_auto_row.set_subtitle(f"[{src}] {best_dll.name} ({best_dll.parent.name})")
            self.fsr411_auto_button.set_sensitive(not building and self.process is None)
        else:
            self.best_detected_dll = None
            self.best_detected_loader = None
            self.fsr411_auto_row.set_subtitle(tr("DLL не найдена в системе"))
            self.fsr411_auto_button.set_sensitive(False)

    def on_fsr411_auto_setup(self, _btn):
        if getattr(self, "best_detected_dll", None):
            self.on_fsr411_dll(str(self.best_detected_dll), str(self.best_detected_loader) if self.best_detected_loader else None)
        else:
            self.toasts.add_toast(Adw.Toast(title=tr("DLL не найдена в системе")))

    # --- Overview page -------------------------------------------------------------------

    def build_overview_page(self):
        page = Adw.PreferencesPage()

        # Game and files
        game = Adw.PreferencesGroup(title=tr("Игра"))
        self.game_row = Adw.ActionRow(title=tr("Папка игры (CUSA03173)"))
        self.game_status = Gtk.Image()
        self.game_row.add_suffix(self.game_status)
        self.game_row.add_suffix(flat_button("folder-open-symbolic", tr("Выбрать папку с eboot.bin"),
                                             self.on_choose_game))
        self.game_row.add_suffix(flat_button("system-file-manager-symbolic",
                                             tr("Открыть в файловом менеджере"),
                                             lambda _b: open_folder(self, self.game_dir())))
        game.add(self.game_row)

        self.user_row = Adw.ActionRow(title=tr("Папка сохранений"))
        self.user_status = Gtk.Image()
        self.user_row.add_suffix(self.user_status)
        self.user_row.add_suffix(flat_button("folder-open-symbolic", tr("Выбрать папку сохранений"),
                                             self.on_choose_user))
        self.user_row.add_suffix(flat_button("system-file-manager-symbolic",
                                             tr("Открыть в файловом менеджере"),
                                             lambda _b: open_folder(self, self.user_dir())))
        self.user_reset = flat_button("edit-undo-symbolic", tr("Вернуть папку по умолчанию"),
                                      self.on_reset_user)
        self.user_row.add_suffix(self.user_reset)
        game.add(self.user_row)

        self.language_row = combo_row(tr("Язык системы"), None, LANGUAGES, self.settings["language"])
        game.add(self.language_row)

        self.ui_language_row = combo_row(tr("Язык лаунчера") + " / Launcher language", None,
                                         UI_LANGUAGES, self.settings.get("ui_language", ""))
        self.ui_language_row.connect("notify::selected", self.on_ui_language)
        game.add(self.ui_language_row)
        page.add(game)

        # Auto: the new memory model on every GPU; or either by hand.
        mode = Adw.PreferencesGroup(title=tr("Режим работы"))
        self.memory_model_row = combo_row(tr("Модель памяти и трансляции"), tr(PC_MODEL_SUBTITLE),
                                          MEMORY_MODELS, self.settings.get("memory_model", "auto"))
        mode.add(self.memory_model_row)
        # The GPU the game runs on, as in shadPS4 (laptops with two GPUs).
        self.gpu_row = Adw.ComboRow(title=tr("Видеокарта"))
        self.gpu_row.add_suffix(flat_button("view-refresh-symbolic", tr("Обновить список"),
                                            lambda _button: self.fill_gpus()))
        self.fill_gpus()
        mode.add(self.gpu_row)
        page.add(mode)

        return page

    def fill_gpus(self):
        """The GPU choices: the port's own (named), the GPUs found, and the saved one while it is
        not found."""
        current = self.settings.get("gpu", "")
        gpus = connected_gpus()
        best = [name for _index, _vendor, _kind, name, is_best in gpus if is_best]
        auto = tr("Авто") + (f" ({best[0]})" if best else "")
        choices = [(auto, "")]
        for _index, _vendor, kind, name, _best in gpus:
            note = {"integrated": tr("встроенная"), "cpu": tr("программная, очень медленно")}.get(kind)
            choices.append((f"{name} ({note})" if note else name, name))
        if current and current not in [value for _, value in choices]:
            choices.append((tr("{} (не найдена)").format(current), current))
        self.gpu_row.set_model(Gtk.StringList.new([label for label, _ in choices]))
        self.gpu_row.values = [value for _, value in choices]
        self.gpu_row.set_selected(self.gpu_row.values.index(current) if current in self.gpu_row.values else 0)
        self.gpu_row.set_subtitle(tr("На ноутбуке с двумя видеокартами — дискретная; применяется при запуске игры"))


    # --- Graphics page -------------------------------------------------------------------

    def build_graphics_page(self):
        page = Adw.PreferencesPage()

        screen = Adw.PreferencesGroup(title=tr("Экран"))
        # The aspect ratio, then its resolutions (rebuilt when it changes, the height kept).
        self.aspect_row = Adw.ComboRow(title=tr("Соотношение сторон"), subtitle=tr(
            "Шире 16:9 — больше обзор по бокам, выше — сверху и снизу; интерфейс у краёв экрана"))
        self.aspect_row.values = ASPECTS
        self.output_row = Adw.ComboRow(title=tr("Разрешение вывода"),
                                       subtitle=tr("Апскейлер дорисовывает кадр; Steam Deck — 1280×800 (16:10)"))
        self.output_row.values = []
        screen.add(self.aspect_row)
        screen.add(self.output_row)
        self.fullscreen_row = Adw.SwitchRow(title=tr("Полноэкранный режим"),
                                            active=self.settings["fullscreen"])
        screen.add(self.fullscreen_row)
        self.live_row = combo_row(tr("Смена разрешения на лету"),
                                  tr("Без перезапуска, но медленнее на Steam Deck и старых GPU"),
                                  LIVE_RESOLUTION, self.ini.get("live_resolution", "0"))
        screen.add(self.live_row)
        self.display_row = Adw.ComboRow(title=tr("Монитор"))
        self.display_row.add_suffix(flat_button("view-refresh-symbolic", tr("Обновить список"),
                                                lambda _button: self.fill_displays()))
        screen.add(self.display_row)
        self.video_backend_row = combo_row(tr("Оконный бэкенд"), None, VIDEO_BACKENDS,
                                           self.settings.get("video_backend", "auto"))
        screen.add(self.video_backend_row)

        self.fill_displays()
        self.fill_aspects(self.ini.get("aspect", "auto"))
        self.fill_outputs(keep=self.ini.get("output_res"))

        self.aspect_row.connect("notify::selected",
                                lambda *_: self.fill_outputs(keep=self.output_row.get_selected_item()))
        self.display_row.connect("notify::selected", lambda *_: self.on_display_changed())
        self.present_row = combo_row(tr("Режим показа кадров"), None, PRESENT_MODES,
                                     self.settings["present_mode"])
        screen.add(self.present_row)
        self.hdr_row = Adw.SwitchRow(title=tr("Разрешить HDR"), active=self.settings["hdr"])
        screen.add(self.hdr_row)
        page.add(screen)

        upscaler = Adw.PreferencesGroup(
            title=tr("Апскейлер"),
            description=tr("Хранится в bbport.ini; в игре меняется через меню (Insert или L3+R3)"))
        self.upscaler_row = combo_row(tr("Апскейлер"), None, UPSCALERS, self.ini["upscaler"])
        self.upscaler_row.connect("notify::selected", lambda *_: self.update_upscaler_status())
        upscaler.add(self.upscaler_row)
        self.dlss_row = Adw.ActionRow(title=tr("DLSS: библиотека NVIDIA"))
        self.dlss_row.add_suffix(flat_button("document-open-symbolic", tr("Выбрать файл…"),
                                             lambda _b: self.choose_ngx()))
        self.dlss_reset_button = flat_button("edit-undo-symbolic", tr("Убрать выбранную"),
                                             lambda _b: self.reset_ngx())
        self.dlss_row.add_suffix(self.dlss_reset_button)
        upscaler.add(self.dlss_row)
        self.update_dlss_row()
        self.preset_row = combo_row(tr("Пресет"), None, PRESETS, int(self.ini.get("preset", "4")))
        self.preset_row.connect("notify::selected", lambda *_: self.update_upscaler_status())
        self.output_row.connect("notify::selected", lambda *_: self.update_upscaler_status())
        upscaler.add(self.preset_row)

        self.sharpen_row = Adw.SwitchRow(title=tr("Резкость (RCAS)"),
                                         active=self.ini.get("sharpen") == "1")
        upscaler.add(self.sharpen_row)
        self.sharpness_row = Adw.SpinRow.new_with_range(0.0, 2.0, 0.05)
        self.sharpness_row.set_title(tr("Сила резкости"))
        self.sharpness_row.set_digits(2)
        self.sharpness_row.set_value(float(self.ini.get("sharpness", "0.5")))
        upscaler.add(self.sharpness_row)
        self.motion_row = Adw.SwitchRow(
            title=tr("Векторы движения объектов"),
            subtitle=tr("Меньше гостинга на персонажах; стоит около 10% FPS"),
            active=self.ini.get("object_motion") == "1")
        upscaler.add(self.motion_row)
        self.frame_gen_row = Adw.SwitchRow(
            title=tr("Генерация кадров (FSR 3.1)"),
            subtitle=tr("Удваивает частоту кадров с помощью интерполяции"),
            active=self.ini.get("frame_generation") == "1")
        upscaler.add(self.frame_gen_row)
        self.show_fps_row = Adw.SwitchRow(title=tr("Показывать FPS"),
                                          active=self.ini.get("show_fps") == "1")
        upscaler.add(self.show_fps_row)
        page.add(upscaler)

        fsr_auto_group = Adw.PreferencesGroup(
            title=tr("Автоматизация FSR 4.1.1"),
            description=tr("FSR 4.1.1 из amd_fidelityfx_upscaler_dx12.dll 4.1.x (OptiScaler / Goverlay)"))

        self.fsr411_auto_row = Adw.ActionRow(title=tr("FSR 4.1.1 из Goverlay / OptiScaler"))
        self.fsr411_auto_button = Gtk.Button(valign=Gtk.Align.CENTER, label=tr("Auto-detectar e Configurar"))
        self.fsr411_auto_button.add_css_class("suggested-action")
        self.fsr411_auto_button.connect("clicked", self.on_fsr411_auto_setup)
        self.fsr411_auto_row.add_suffix(self.fsr411_auto_button)
        fsr_auto_group.add(self.fsr411_auto_row)

        self.fsr411_row = Adw.ActionRow(title=tr("Выбрать DLL вручную…"))
        self.fsr411_button = Gtk.Button(valign=Gtk.Align.CENTER)
        self.fsr411_button.connect("clicked", self.on_fsr411_button)
        self.fsr411_row.add_suffix(self.fsr411_button)
        fsr_auto_group.add(self.fsr411_row)
        page.add(fsr_auto_group)
        self.update_fsr411_row()
        self.update_fsr411_auto_row()

        effects = Adw.PreferencesGroup(title=tr("Эффекты игры"),
                                       description=tr("Патчи игры, применяются при запуске"))
        self.lod_row = combo_row(tr("Детализация моделей"), None, MODEL_LOD,
                                 self.ini.get("model_lod", "0"))
        effects.add(self.lod_row)
        self.effect_rows = {}
        for key, title, default in EFFECTS:
            row = Adw.SwitchRow(title=tr(title),
                                active=self.ini.get(key, "1" if default else "0") == "1")
            if key == "debug_menu":
                row.set_subtitle(tr("Нужна папка adhoc из мода Nexus #253 (шрифты adhoc/font) — в "
                                    "dvdroot_ps4 игры или модом; без неё патч не применяется"))
            elif key == "puddle_reflections":
                row.set_subtitle(tr("Отражения в лужах"))
            self.effect_rows[key] = row
            effects.add(row)
        self.skip_network_row = Adw.SwitchRow(
            title=tr("Пропускать выбор «по сети / вне сети»"),
            subtitle=tr("Игра сразу открывает главное меню в режиме вне сети (патч игры)"),
            active=self.settings.get("skip_network_choice", True))
        effects.add(self.skip_network_row)
        page.add(effects)

        cache_group = Adw.PreferencesGroup(
            title=tr("Кэш шейдеров"),
            description=tr("Очистка шейдеров и конвейеров полезна при графических артефактах или ошибках освещения"))
        self.clear_cache_row = Adw.ActionRow(
            title=tr("Очистить все шейдеры"),
            subtitle=tr("Вычисляет размер…"))
        self.clear_cache_row.add_suffix(flat_button("system-file-manager-symbolic",
                                                    tr("Открыть папку кэша"),
                                                    lambda _b: open_folder(self, self.user_dir() / "cache")))
        self.clear_cache_button = Gtk.Button(valign=Gtk.Align.CENTER, label=tr("Очистить…"))
        self.clear_cache_button.add_css_class("destructive-action")
        self.clear_cache_button.connect("clicked", self.on_clear_shaders)
        self.clear_cache_row.add_suffix(self.clear_cache_button)
        cache_group.add(self.clear_cache_row)
        page.add(cache_group)
        self.update_cache_status()

        return page

    # --- Performance page ----------------------------------------------------------------

    def build_performance_page(self):
        page = Adw.PreferencesPage()

        frames = Adw.PreferencesGroup(title=tr("Частота кадров"))
        self.fps_row = combo_row(tr("Режим"), tr("Какой патч частоты кадров применить к игре"),
                                 FPS_MODES, self.settings["fps_mode"])
        frames.add(self.fps_row)
        self.limit_row = Adw.SpinRow.new_with_range(0, 480, 1)
        self.limit_row.set_title(tr("Ограничение FPS"))
        self.limit_row.set_subtitle(tr("0 — без ограничения; укажите число, чтобы ограничить FPS"))
        self.limit_row.set_value(self.settings["fps_limit"])
        frames.add(self.limit_row)
        page.add(frames)

        perf = Adw.PreferencesGroup(title=tr("Производительность GPU"))
        self.pipe_row = combo_row(
            tr("Двухстадийный конвейер GPU"),
            tr("Быстрее на 20–30%; при нестабильности выключите"), DRAW_PIPE,
            self.settings["draw_pipe"])
        perf.add(self.pipe_row)
        self.record_threads_row = combo_row(
            tr("Потоки записи Vulkan"),
            tr("Параллельная запись команд; ускоряет рендеринг при упоре в CPU"),
            VK_RECORD_THREADS, self.settings.get("vk_record_threads", "auto"))
        perf.add(self.record_threads_row)
        page.add(perf)

        vram = Adw.PreferencesGroup(title=tr("Память и VRAM"))
        self.preupload_row = combo_row(
            tr("Фоновая загрузка в видеопамять"),
            tr("Меньше рывков при подгрузке зон"), PREUPLOAD,
            self.settings["preupload"])
        vram.add(self.preupload_row)
        self.readbacks_row = combo_row(
            tr("Чтение данных GPU процессором"),
            tr("Для поиска сбоев: чтение текстур обратно в память"), READBACKS,
            self.settings["readbacks"])
        vram.add(self.readbacks_row)
        page.add(vram)

        dev = Adw.PreferencesGroup(title=tr("Для разработчика"))
        self.mangohud_row = Adw.SwitchRow(title="MangoHud", active=self.settings["mangohud"])
        dev.add(self.mangohud_row)
        self.stats_row = Adw.SwitchRow(title=tr("Статистика кадров в журнале"),
                                       active=self.settings["frame_stats"])
        dev.add(self.stats_row)
        self.save_log_row = Adw.SwitchRow(
            title=tr("Сохранять журнал и статистику в файл"),
            subtitle=tr("В папку logs в каталоге данных: для разбора рывков и вылетов"),
            active=self.settings["save_log"])
        dev.add(self.save_log_row)
        self.crash_diag_row = Adw.SwitchRow(
            title=tr("Диагностика вылетов"),
            subtitle=tr("Проверяет кучу игры и записывает записи в её память; немного медленнее"),
            active=self.settings["crash_diag"])
        dev.add(self.crash_diag_row)
        self.as_0_3_row = Adw.SwitchRow(
            title=tr("Синхронизация как в 0.3"),
            subtitle=tr("Для поиска регрессий: старая модель памяти и ожидания копий как в релизе 0.3"),
            active=self.settings["as_0_3"])
        dev.add(self.as_0_3_row)
        self.profile_row = Adw.SwitchRow(title=tr("Профиль GPU в журнале"),
                                         active=self.settings["gpu_profile"])
        dev.add(self.profile_row)
        self.validation_row = Adw.SwitchRow(
            title=tr("Слои валидации Vulkan"),
            subtitle=tr("Сильно замедляет"), active=self.settings["vk_validation"])
        dev.add(self.validation_row)
        self.extra_row = Adw.EntryRow(title=tr("Доп. переменные (ИМЯ=значение через пробел)"),
                                      text=self.settings["extra_env"])
        dev.add(self.extra_row)
        page.add(dev)

        return page

    # --- UI Helpers & Status -------------------------------------------------------------

    def on_ui_language(self, _row, _pspec):
        choice = combo_value(self.ui_language_row)
        if choice != self.settings.get("ui_language"):
            self.settings["ui_language"] = choice
            set_language(choice)
            self.store()
            if self.process:
                self.toasts.add_toast(Adw.Toast(
                    title=tr("Применится после перезапуска лаунчера, пока игра запущена")))
                return
            GLib.idle_add(lambda: self.build() and False)

    def game_dir(self):
        return Path(self.settings["game_dir"]).expanduser()

    def user_dir(self):
        return Path(self.settings["user_dir"]).expanduser() if self.settings["user_dir"] \
            else DATA_DIR / "user"

    def update_user_status(self):
        path = self.user_dir()
        custom = bool(self.settings["user_dir"])
        self.user_row.set_subtitle(str(path) if custom else tr("По умолчанию: {}").format(path))
        saves = path / "savedata"
        found = saves.is_dir() and any(saves.iterdir())
        self.user_status.set_from_icon_name("object-select-symbolic" if found else "document-new-symbolic")
        self.user_status.set_tooltip_text(tr("Найдены сохранения") if found
                                          else tr("Сохранений пока нет: игра создаст их здесь"))
        self.user_reset.set_sensitive(custom)

    def choose_folder(self, title, current, done):
        dialog = Gtk.FileDialog(title=title)
        if Path(current).is_dir():
            dialog.set_initial_folder(Gio.File.new_for_path(str(current)))

        def finish(dialog, result):
            try:
                folder = dialog.select_folder_finish(result)
            except GLib.Error:
                return
            if folder:
                done(folder.get_path())
        dialog.select_folder(self, None, finish)

    def on_choose_user(self, _button):
        def chosen(path):
            self.settings["user_dir"] = path
            self.update_user_status()
            if hasattr(self, "update_cache_status"):
                self.update_cache_status()
            self.store()
        self.choose_folder(tr("Папка сохранений"), self.user_dir(), chosen)

    def on_reset_user(self, _button):
        self.settings["user_dir"] = ""
        self.update_user_status()
        if hasattr(self, "update_cache_status"):
            self.update_cache_status()
        self.store()

    def update_upscaler_status(self):
        if not hasattr(self, "upscaler_row"):
            return
        value = combo_value(self.upscaler_row)
        if value == "fsr4":
            ok = (PORT_DIR / "fsr4_shaders").is_dir()
            hint = tr("Ассеты найдены") if ok else tr("Нет ассетов: tools/fetch_fsr4_assets.sh")
        elif value == "fsr411":
            if not hasattr(self, "output_row") or not hasattr(self, "preset_row"):
                return
            directory = fsr411_dir()
            problem = fsr411_problem(directory, combo_value(self.output_row),
                                     int(combo_value(self.preset_row)))
            if not problem:
                has_fp8 = (directory / "fp8").is_dir()
                hint = tr("Ассеты найдены (FP8 / Float8 RDNA4)") if has_fp8 else tr("Ассеты для выбранного режима найдены")
            else:
                hint = tr("{}. Соберите FSR 4.1.1 из своей DLL кнопкой «Выбрать DLL…» ниже").format(problem)
        elif value == "dlss":
            binaries = PORT_DIR / ("bin" if PACKAGED else "out")
            found = (binaries / "libbbport_dlss.so").is_file() and bool(
                ngx_libraries(self.user_dir() / "dlss") or ngx_libraries(binaries))
            hint = tr("DLSS найден (нужна видеокарта NVIDIA RTX)") if found else tr(
                "DLSS не найден: выберите библиотеку NVIDIA ниже (и нужна сборка с мостом DLSS). Игра включит FSR 3.1")
        else:
            hint = tr("Сглаживание в разрешении вывода без модели FSR") if value == "taa" else None
        if hasattr(self, "preset_row"):
            self.preset_row.set_sensitive(value not in ("taa", "off"))
        if hasattr(self, "sharpen_row"):
            self.sharpen_row.set_sensitive(value != "off")
        if hasattr(self, "sharpness_row"):
            self.sharpness_row.set_sensitive(value != "off")
        self.upscaler_row.set_subtitle(hint or "")

    def game_problem(self, path):
        """game_check.problem for the folder, kept until its eboot.bin or param.sfo changes (the
        image hash reads ~90 MB)."""
        try:
            key = (str(path),) + tuple(
                (f.stat().st_size, f.stat().st_mtime_ns) for f in (path / "eboot.bin", path / "sce_sys/param.sfo"))
        except OSError:
            key = (str(path),)
        if getattr(self, "_game_problem_key", None) != key:
            self._game_problem_key = key
            self._game_problem = game_check.problem(path)
        return self._game_problem

    def update_game_status(self):
        path = self.game_dir()
        ok = bool(self.settings["game_dir"]) and (path / "eboot.bin").is_file()
        self.game_row.set_subtitle(str(path) if self.settings["game_dir"] else tr("не выбрана"))
        # Other versions of the game start and then crash in its code (issues #7, #13, #14).
        problem = self.game_problem(path) if ok else None
        if not ok:
            tooltip = tr("Нет eboot.bin в папке")
        elif problem:
            kind, title, version = problem
            tooltip = {
                "missing_update": tr("Нужно обновление 1.09: скопируйте файлы дампа обновления 1.09 в папку игры с заменой (найдена версия {})").format(version),
                "wrong_eboot": tr("eboot.bin не от версии 1.09: скопируйте eboot.bin из дампа обновления 1.09 в папку игры с заменой"),
                "patched_eboot": tr("eboot.bin с вшитым патчем 60 FPS от Lance McDonald: из-за него игра падает в меню жестов. Скопируйте чистый eboot.bin из дампа обновления 1.09 в папку игры с заменой"),
                "other_title": tr("Это не магазинное издание Bloodborne (найдено {}): нужен Bloodborne любого региона с обновлением 1.09").format(title),
                "unreadable": tr("eboot.bin не читается как расшифрованный исполняемый файл PS4: сделайте дамп заново"),
                "damaged_files": tr("Файлы игры повреждены при распаковке: шейдеры не распаковываются, игра зависнет на загрузке. Распакуйте игру и обновление 1.09 заново исправленным инструментом (issue #81)"),
            }[kind]
            self.game_row.set_subtitle(f"{path}\n{tooltip}")
        else:
            tooltip = tr("Bloodborne CUSA03173, версия 1.09")
        self.game_status.set_from_icon_name("object-select-symbolic" if ok and not problem else "dialog-warning-symbolic")
        self.game_status.set_tooltip_text(tooltip)
        self.update_launch_button()

    def on_choose_game(self, _button):
        def chosen(path):
            self.settings["game_dir"] = path
            self.update_game_status()
            self.store()
        self.choose_folder(tr("Папка игры (CUSA03173)"), self.game_dir(), chosen)

    def monitor_size(self):
        displays = getattr(self.display_row, "displays", [])
        index = self.display_row.get_selected()
        if 0 <= index < len(displays):
            _val, _lbl, size, _primary = displays[index]
            return size
        return None

    def fill_aspects(self, current):
        choices = [
            (tr("Авто") + (f" ({resolved_aspect('auto', self.monitor_size())})" if self.monitor_size() else ""), "auto"),
            ("4:3", "4:3"),
            ("16:10", "16:10"),
            ("16:9", "16:9"),
            ("21:9", "21:9"),
            ("32:9", "32:9"),
        ]
        self.aspect_row.set_model(Gtk.StringList.new([label for label, _ in choices]))
        self.aspect_row.values = [val for _, val in choices]
        selected = self.aspect_row.values.index(current) if current in self.aspect_row.values else 0
        self.aspect_row.set_selected(selected)

    def fill_outputs(self, keep=None):
        aspect = combo_value(self.aspect_row)
        sizes = output_sizes(aspect, self.monitor_size())
        texts = [f"{w}×{h}" for w, h in sizes]
        values = [f"{w}x{h}" for w, h in sizes]
        keep_size = parse_size(keep if isinstance(keep, str) else keep.get_string() if keep else "")
        target = nearest_output(sizes, keep_size) if keep else sizes[-1]
        self.output_row.set_model(Gtk.StringList.new(texts))
        self.output_row.values = values
        self.output_row.set_selected(sizes.index(target) if target in sizes else len(sizes) - 1)
        self.update_upscaler_status()

    def on_display_changed(self):
        if not hasattr(self, "aspect_row") or not hasattr(self, "output_row"):
            return
        aspect = combo_value(self.aspect_row)
        keep = self.output_row.get_selected_item()
        self.fill_aspects(aspect)
        self.fill_outputs(keep=keep)

    def store(self):
        s = self.settings
        s["theme_mode"] = getattr(self, "theme_mode", "dark")
        s["aspect"] = combo_value(self.aspect_row)
        s["gamepad"] = combo_value(self.gamepad_row)
        s["gamepad_name"] = self.gamepad_row.names[self.gamepad_row.get_selected()]
        s["display"] = combo_value(self.display_row)
        s["video_backend"] = combo_value(self.video_backend_row)
        s["gpu"] = combo_value(self.gpu_row)
        s["skip_network_choice"] = self.skip_network_row.get_active()
        s["language"] = combo_value(self.language_row)
        s["fullscreen"] = self.fullscreen_row.get_active()
        s["present_mode"] = combo_value(self.present_row)
        s["hdr"] = self.hdr_row.get_active()
        s["fps_mode"] = combo_value(self.fps_row)
        s["fps_limit"] = int(self.limit_row.get_value())
        s["draw_pipe"] = combo_value(self.pipe_row)
        s["vk_record_threads"] = combo_value(self.record_threads_row)
        s["readbacks"] = combo_value(self.readbacks_row)
        s["preupload"] = combo_value(self.preupload_row)
        s["mangohud"] = self.mangohud_row.get_active()
        s["frame_stats"] = self.stats_row.get_active()
        s["save_log"] = self.save_log_row.get_active()
        s["crash_diag"] = self.crash_diag_row.get_active()
        s["memory_model"] = combo_value(self.memory_model_row)
        s["as_0_3"] = self.as_0_3_row.get_active()
        s["gpu_profile"] = self.profile_row.get_active()
        s["vk_validation"] = self.validation_row.get_active()
        s["extra_env"] = self.extra_row.get_text().strip()
        s["mods_enabled"] = self.mods_enabled_row.get_active()
        s["online"] = self.online_row.get_active()
        s["online_bbsc"] = self.bbsc_row.get_active() if hasattr(self, "bbsc_row") else s.get("online_bbsc", False)
        s["online_net_gen"] = combo_value(self.net_gen_row) if hasattr(self, "net_gen_row") else s.get("online_net_gen", "19")
        s["online_community"] = self.community_row.get_text().strip()
        s["online_server"] = self.server_row.get_text().strip()
        s["online_webapi"] = self.webapi_row.get_text().strip()
        s["online_npid"] = self.npid_row.get_text().strip()
        s["online_password"] = self.password_row.get_text()
        s["online_upnp"] = self.upnp_row.get_active()
        if hasattr(self, "pool_row"):
            s["online_pool"] = self.online_pool()
        if hasattr(self, "thd_server_row"):
            s["thd_server"] = self.thd_server_row.get_text().strip()
            s["thd_online_id"], s["thd_token"] = self.thd_id, self.thd_token
        if hasattr(self, "backend_row"):
            s["online_backend"] = combo_value(self.backend_row)
        if hasattr(self, "bbhost_server_row"):
            s["online_bbhost_server"] = self.bbhost_server_row.get_text().strip()
        if hasattr(self, "discord_cid_row"):
            cid = self.discord_cid_row.get_text().strip() or "1560890616811102219"
            s["discord_client_id"] = cid
            self.ini["discord_client_id"] = cid
        self.save_mod_profile()
        self.save_patch_profile()
        save_settings(s)
        self.ini.update({
            "upscaler": combo_value(self.upscaler_row),
            "preset": str(combo_value(self.preset_row)),
            "sharpen": "1" if self.sharpen_row.get_active() else "0",
            "sharpness": f"{self.sharpness_row.get_value():.2f}",
            "object_motion": "1" if self.motion_row.get_active() else "0",
            "frame_generation": "1" if self.frame_gen_row.get_active() else "0",
            "show_fps": "1" if self.show_fps_row.get_active() else "0",
            "aspect": combo_value(self.aspect_row),
            "output_res": combo_value(self.output_row),
            "video_backend": combo_value(self.video_backend_row),
            "model_lod": combo_value(self.lod_row),
            "live_resolution": combo_value(self.live_row),
            "mouse_look": "1" if (hasattr(self, "mouse_look_row") and self.mouse_look_row.get_active()) else self.ini.get("mouse_look", "1"),
            **({
                "mouse_sensitivity": f"{self.mouse_sens_row.get_value():.2f}",
                "mouse_invert_y": "1" if self.mouse_inv_y_row.get_active() else "0",
                "mouse_invert_x": "1" if self.mouse_inv_x_row.get_active() else "0",
                "mouse_capture": "1" if self.mouse_capture_row.get_active() else "0",
            } if hasattr(self, "mouse_sens_row") else {}),
            **({"disable_gyro": "1" if self.disable_gyro_row.get_active() else "0"} if hasattr(self, "disable_gyro_row") else {}),
            **{key: "1" if row.get_active() else "0" for key, row in self.effect_rows.items()},
            "menu_language": "pt" if self.settings.get("ui_language") == "pt_BR" else (
                "ru" if self.settings.get("ui_language") == "ru" else (
                    "en" if self.settings.get("ui_language") == "en" else self.ini.get("menu_language", "pt")
                )
            ),
            "fps_limit": str(s["fps_limit"]),
        })
        save_ini(self.ini, self.ini_lines)
        self.ini, self.ini_lines = load_ini()

    def environment(self):
        return game_environment(self.settings)

    # --- FSR 4.1.1 from the user's DLL -----------------------------------------------------

    def update_fsr411_row(self, progress=None):
        self.fsr411_mgr.update_row(progress)

    def alert(self, heading, body):
        if hasattr(Adw, "AlertDialog"):
            dialog = Adw.AlertDialog(heading=heading, body=body)
            dialog.add_response("ok", "OK")
            dialog.present(self)
        else:
            self.toasts.add_toast(Adw.Toast(title=f"{heading}: {body}", timeout=10))

    def choose_dll(self, title, done):
        self.fsr411_mgr.choose_dll(title, done)

    def on_fsr411_button(self, _button):
        self.fsr411_mgr.on_button_clicked()

    def on_fsr411_dll(self, upscaler, loader=None):
        self.fsr411_mgr.on_dll_selected(upscaler, loader)

    def start_fsr411_build(self, upscaler, loader):
        self.fsr411_mgr.start_build(upscaler, loader)

    def stop_fsr411_build(self):
        self.fsr411_mgr.stop()


class LauncherApp(Adw.Application):
    def __init__(self):
        flags = getattr(Gio.ApplicationFlags, "NON_UNIQUE", 0)
        super().__init__(application_id="io.github.bbport.Launcher", flags=flags)
        self.startup_t0 = time.monotonic()

    def do_startup(self):
        LOG.info("startup.gtk.begin")
        try:
            Adw.Application.do_startup(self)
        except Exception as e:
            LOG.exception("[E-GTK-001] startup.gtk.failed: falha ao inicializar backend gráfico GTK: %s", e)
            raise
        setup_icons()
        display = Gdk.Display.get_default()
        if not display:
            LOG.error("[E-GTK-001] startup.gtk.failed: Gdk.Display.get_default() retornou None. O backend gráfico não pôde conectar ao servidor de exibição.")
        else:
            LOG.info("startup.gtk.ok: active_backend=%s", get_runtime_backend(display))

    def do_activate(self):
        setup_icons()
        window = self.get_active_window()
        if not window:
            LOG.info("startup.application.begin")
            window = LauncherWindow(self)
            LOG.info("startup.application.ok")
        window.present()
        t_total = (time.monotonic() - getattr(self, "startup_t0", time.monotonic())) * 1000.0
        LOG.info("startup.completed: total_elapsed=%.1fms", t_total)


def get_launcher_version():
    ver_file = Path(__file__).resolve().parent / "VERSION"
    if ver_file.is_file():
        try:
            return ver_file.read_text(encoding="utf-8").strip()
        except OSError:
            pass
    try:
        res = subprocess.run(["git", "describe", "--tags", "--always"],
                             cwd=Path(__file__).resolve().parent.parent,
                             capture_output=True, text=True, timeout=1.0)
        if res.returncode == 0 and res.stdout.strip():
            ver = res.stdout.strip()
            return ver[1:] if ver.startswith("v") else ver
    except Exception:
        pass
    return "0.6.2"


def play():
    """--play: the game with the saved settings, no window (Steam Deck game mode)."""
    settings = load_settings()
    set_language(settings.get("ui_language", ""))
    if not (Path(settings["game_dir"]).expanduser() / "eboot.bin").is_file():
        print("bbport: choose the game folder in the launcher first", file=sys.stderr)
        return 1
    os.chdir(PORT_DIR)
    os.execvpe("bash", ["bash", str(PORT_DIR / "run.sh")], game_environment(settings))


def build_fsr411(arguments):
    """--build-fsr411 <upscaler DLL> [loader DLL]: the launcher's FSR 4.1.1 build, without a window."""
    if not 1 <= len(arguments) <= 2:
        print("usage: --build-fsr411 <amd_fidelityfx_upscaler_dx12.dll> [loader DLL]", file=sys.stderr)
        return 1
    command, env = fsr411_build_command(*arguments)
    return subprocess.call(command, env=env, cwd=PORT_DIR)


def main():
    if "--play" in sys.argv[1:]:
        return play()
    if "--build-fsr411" in sys.argv[1:]:
        return build_fsr411(sys.argv[sys.argv.index("--build-fsr411") + 1:])
    if "--diagnostics" in sys.argv[1:]:
        print(collect_diagnostics_report())
        return 0
    if "--version" in sys.argv[1:] or "-v" in sys.argv[1:]:
        print(f"BBPort Launcher {get_launcher_version()}")
        return 0
    if "--help" in sys.argv[1:] or "-h" in sys.argv[1:]:
        print(f"BBPort Launcher {get_launcher_version()} - Bloodborne PC Port Launcher")
        print("\nUso:")
        print("  bbport-launcher [opções]\n")
        print("Opções:")
        print("  --play             Inicia o jogo diretamente com as configurações salvas (Steam Deck / Game Mode)")
        print("  --diagnostics      Gera e exibe relatório de diagnóstico do sistema e encerra")
        print("  --version, -v      Exibe a versão do launcher e encerra")
        print("  --help, -h         Exibe esta mensagem de ajuda e encerra")
        print("  --x11              Força o uso do backend X11/XWayland")
        print("  --wayland          Força o uso do backend Wayland nativo")
        print("  --build-fsr411     Compila os shaders FSR 4.1.1 em segundo plano")
        return 0

    LOG.info("startup.preflight.begin")
    if sys.version_info < (3, 9):
        LOG.error("startup.preflight.failed: Python >= 3.9 required, found %s", sys.version)
        print("Error: Python >= 3.9 is required to run BBPort Launcher.", file=sys.stderr)
        return 1

    # Preflight: Check display server availability for graphical session
    if not os.environ.get("WAYLAND_DISPLAY") and not os.environ.get("DISPLAY"):
        err_msg = "[E-DISP-001] Nenhum display gráfico detectado (WAYLAND_DISPLAY e DISPLAY não definidos). Execute em uma sessão gráfica ou use --diagnostics / --play."
        LOG.error("startup.preflight.failed: %s", err_msg)
        print(f"Erro: {err_msg}", file=sys.stderr)
        return 1

    LOG.info("startup.preflight.ok")

    # Pass only program name and recognized GTK/GIO flags so file managers
    # passing file URLs/paths (like Dolphin) do not trigger GLib "cannot open files" error
    gtk_args = [sys.argv[0]] + [arg for arg in sys.argv[1:] if arg.startswith("--g-") or arg.startswith("--gtk-")]
    app = LauncherApp()
    return app.run(gtk_args)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception:
        LOG.exception("startup.fatal: unrecoverable exception")
        sys.exit(1)
