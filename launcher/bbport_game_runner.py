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
        self.update_launch_button()
        self.update_game_status()
        if hasattr(self, "update_cache_status"):
            self.update_cache_status()
        if hasattr(self, "log_status_badge"):
            self.log_status_badge.set_label(tr("Игра не запущена"))
            self.log_status_badge.remove_css_class("badge-success")
            self.log_status_badge.add_css_class("badge-warning")
        self.append_log(tr("\n— игра завершилась (код {}) —\n").format(status))

    def on_close(self, _window):
        self.store()
        if hasattr(self, "gamepad_nav") and self.gamepad_nav:
            self.gamepad_nav.close()
        if self.process:
            self.stop_game()
        if getattr(self, "fsr411_build", None):
            self.stop_fsr411_build()
        return False
