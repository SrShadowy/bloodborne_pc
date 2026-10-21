# SPDX-License-Identifier: GPL-2.0-or-later
"""Game process lifecycle management, subprocess launcher, and I/O streaming for BBPort."""

import os
import signal
from bbport_config import PORT_DIR, online_module
from bbport_i18n import tr

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, Gio, GLib  # noqa: E402


class GameRunnerMixin:
    """Mixin for LauncherWindow handling game execution, process life-cycle, and I/O."""

    def on_launch(self, _button):
        if self.process:
            self.stop_game()
            return
        npid = self.npid_row.get_text().strip()
        if online_module() and not self.online_row.get_active() and npid and hasattr(Adw, "AlertDialog"):
            dialog = Adw.AlertDialog(heading="Bloodborne", body=tr(
                "Учётная запись для игры онлайн указана, но «Играть онлайн» выключено"))
            dialog.add_response("cancel", tr("Отмена"))
            dialog.add_response("offline", tr("Играть офлайн"))
            dialog.add_response("online", tr("Играть онлайн"))
            dialog.set_response_appearance("online", Adw.ResponseAppearance.SUGGESTED)

            def on_response(_dialog, response):
                if response != "cancel":
                    self.online_row.set_active(response == "online")
                    self.start_game()
            dialog.connect("response", on_response)
            dialog.present(self)
            return
        self.start_game()

    def start_game(self):
        if self.online_row.get_active() and "@" in self.npid_row.get_text():
            self.alert("Bloodborne", tr("Online ID — это имя учётной записи (NPID), "
                                        "зарегистрированное на сервере, а не адрес email"))
            self.stack.set_visible_child_name("online")
            return
        self.store()
        self.log_view.get_buffer().set_text("")
        launcher = Gio.SubprocessLauncher.new(
            Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_MERGE)
        launcher.set_environ([f"{k}={v}" for k, v in self.environment().items()])
        launcher.set_cwd(str(PORT_DIR))
        try:
            # setsid: the game and its helpers form one process group, stopped together.
            self.process = launcher.spawnv(["setsid", "bash", str(PORT_DIR / "run.sh")])
        except GLib.Error as error:
            self.toasts.add_toast(Adw.Toast(title=tr("Не удалось запустить: {}").format(error.message)))
            return
        self.stream = Gio.DataInputStream.new(self.process.get_stdout_pipe())
        self.read_line()
        self.process.wait_async(None, self.on_exit)
        self.update_launch_button()
        if hasattr(self, "log_status_badge"):
            pid = self.process.get_identifier()
            self.log_status_badge.set_label(tr("Игра запущена (PID {})").format(pid))
            self.log_status_badge.remove_css_class("badge-warning")
            self.log_status_badge.add_css_class("badge-success")
        self.stack.set_visible_child_name("log")

    def read_line(self):
        self.stream.read_line_async(GLib.PRIORITY_DEFAULT, None, self.on_line)

    def on_line(self, stream, result):
        try:
            line, _length = stream.read_line_finish_utf8(result)
        except GLib.Error:
            return
        if line is None:
            return
        self.append_log(line + "\n")
        self.read_line()

    def stop_game(self):
        if not self.process:
            return
        pid = int(self.process.get_identifier())
        try:
            os.killpg(pid, signal.SIGTERM)
        except OSError:
            self.process.force_exit()
        GLib.timeout_add_seconds(3, self.kill_if_running, pid)

    def kill_if_running(self, pid):
        if self.process:
            try:
                os.killpg(pid, signal.SIGKILL)
            except OSError:
                pass
        return False

    def on_exit(self, process, result):
        try:
            process.wait_finish(result)
        except GLib.Error:
            pass
        status = process.get_exit_status() if process.get_if_exited() else -1
        self.process = None
        self.reload_ini_from_disk()
        self.update_launch_button()
        self.update_game_status()
        if hasattr(self, "update_cache_status"):
            self.update_cache_status()
        if hasattr(self, "log_status_badge"):
            self.log_status_badge.set_label(tr("Игра не запущена"))
            self.log_status_badge.remove_css_class("badge-success")
            self.log_status_badge.add_css_class("badge-warning")
        self.append_log(tr("\n— игра завершилась (код {}) —\n").format(status))

    def reload_ini_from_disk(self):
        """Reloads bbport.ini from disk and refreshes all launcher widgets that display ini values."""
        from bbport_config import load_ini
        self.ini, self.ini_lines = load_ini()
        # Refresh graphics page widgets that mirror bbport.ini values
        self._sync_ini_to_widgets()

    def _sync_ini_to_widgets(self):
        """Updates launcher UI widgets from the reloaded self.ini dict."""
        from bbport_ui_helpers import combo_value
        def set_combo(row, value):
            if not hasattr(row, "values"):
                return
            for i, v in enumerate(row.values):
                if str(v) == str(value):
                    row.set_selected(i)
                    return
        # Upscaler settings
        if hasattr(self, "upscaler_row"):
            set_combo(self.upscaler_row, self.ini.get("upscaler", "fsr4"))
        if hasattr(self, "preset_row"):
            set_combo(self.preset_row, self.ini.get("preset", "4"))
        if hasattr(self, "sharpen_row"):
            self.sharpen_row.set_active(self.ini.get("sharpen") == "1")
        if hasattr(self, "sharpness_row"):
            try:
                self.sharpness_row.set_value(float(self.ini.get("sharpness", "0.50")))
            except (ValueError, TypeError):
                pass
        if hasattr(self, "motion_row"):
            self.motion_row.set_active(self.ini.get("object_motion") == "1")
        if hasattr(self, "frame_gen_row"):
            self.frame_gen_row.set_active(self.ini.get("frame_generation") == "1")
        if hasattr(self, "show_fps_row"):
            self.show_fps_row.set_active(self.ini.get("show_fps") == "1")
        if hasattr(self, "lod_row"):
            set_combo(self.lod_row, self.ini.get("model_lod", "0"))
        if hasattr(self, "live_row"):
            set_combo(self.live_row, self.ini.get("live_resolution", "0"))
        if hasattr(self, "video_backend_row"):
            set_combo(self.video_backend_row, self.ini.get("video_backend", "auto"))
        # Effects
        if hasattr(self, "effect_rows"):
            for key, row in self.effect_rows.items():
                row.set_active(self.ini.get(key, "1") == "1")
        # Mouse settings
        if hasattr(self, "mouse_sens_row"):
            try:
                self.mouse_sens_row.set_value(float(self.ini.get("mouse_sensitivity", "1.00")))
            except (ValueError, TypeError):
                pass
        if hasattr(self, "mouse_inv_y_row"):
            self.mouse_inv_y_row.set_active(self.ini.get("mouse_invert_y") == "1")
        if hasattr(self, "mouse_inv_x_row"):
            self.mouse_inv_x_row.set_active(self.ini.get("mouse_invert_x") == "1")
        if hasattr(self, "mouse_capture_row"):
            self.mouse_capture_row.set_active(self.ini.get("mouse_capture") == "1")
        if hasattr(self, "disable_gyro_row"):
            self.disable_gyro_row.set_active(self.ini.get("disable_gyro") == "1")
        if hasattr(self, "mouse_look_row"):
            self.mouse_look_row.set_active(self.ini.get("mouse_look") == "1")
        # FPS limit (stored in both ini and settings.json)
        if hasattr(self, "limit_row"):
            try:
                self.limit_row.set_value(int(self.ini.get("fps_limit", "0")))
            except (ValueError, TypeError):
                pass
        # Update dependent status displays
        if hasattr(self, "update_upscaler_status"):
            self.update_upscaler_status()

    def on_close(self, _window):
        self.store()
        if hasattr(self, "gamepad_nav") and self.gamepad_nav:
            self.gamepad_nav.close()
        if self.process:
            self.stop_game()
        if getattr(self, "fsr411_build", None):
            self.stop_fsr411_build()
        return False
