# SPDX-License-Identifier: GPL-2.0-or-later
"""UI tests for the Online mixin's Hunter's Dream account group (bbport_online).

The mixin runs against real GTK4/libadwaita widgets (like tests/test_gamepad.py)
with fake toasts/alerts and a monkeypatched protocol client: no network, no
browser, no game. The handlers hop to a thread and back via GLib.idle_add, so
each step is followed by a main-loop pump.
"""
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
for extra in ("launcher", "scripts"):
    if str(ROOT / extra) not in sys.path:
        sys.path.insert(0, str(ROOT / extra))

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, GLib  # noqa: E402

import bbport_bbhost as bbhost  # noqa: E402
import bbport_config  # noqa: E402
import bbport_shadnet  # noqa: E402
from bbport_online import OnlineMixin  # noqa: E402
import bbport_bbhost_ui  # noqa: E402
from bbport_bbhost_ui import BbhostAccountMixin  # noqa: E402

Adw.init()

LINK = {"DeviceCode": "dc", "UserCode": "ABCD-1234",
        "VerificationUri": "https://example.com/link", "Interval": 5, "ExpiresIn": 600}


class _Toasts:
    def __init__(self):
        self.items = []

    def add_toast(self, toast):
        self.items.append(toast)


class FakeWindow(OnlineMixin, BbhostAccountMixin):
    """The mixins on bare widgets: settings, toasts and alerts are fakes."""

    def __init__(self, directory):
        self.settings = dict(bbport_config.DEFAULTS,
                             online_bbhost_server="", online_backend="bbhost")
        self.toasts = _Toasts()
        self.alerts = []
        self.account_file = Path(directory) / "account.json"
        self.page = self.build_online_page()

    def alert(self, heading, body):
        self.alerts.append((heading, body))


def pump(seconds=1.0):
    """Run the main loop so the handlers' GLib.idle_add hops land."""
    loop = GLib.MainLoop()
    GLib.timeout_add(int(seconds * 1000), loop.quit)
    loop.run()


def contains(parent, widget):
    """True when `widget` is anywhere in `parent`'s GTK tree: built is not
    added — the group must be a child of the page to show at all."""
    child = parent.get_first_child()
    while child:
        if child is widget or contains(child, widget):
            return True
        child = child.get_next_sibling()
    return False


def pump_until(check, seconds=5.0):
    """Pump until the condition holds (a watchdog gives up after `seconds`)."""
    deadline = time.monotonic() + seconds
    loop = GLib.MainLoop()

    def tick():
        if check() or time.monotonic() > deadline:
            loop.quit()
        else:
            GLib.timeout_add(20, tick)
        return False

    GLib.timeout_add(20, tick)
    loop.run()
    return check()


class OnlineUiCase(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        for patch in (mock.patch.object(bbport_bbhost_ui, "ACCOUNT_FILE",
                                        Path(self.directory.name) / "account.json"),
                      mock.patch.object(bbport_bbhost_ui.Gio.AppInfo,
                                        "launch_default_for_uri", lambda *args: None)):
            patch.start()
            self.addCleanup(patch.stop)
        self.window = FakeWindow(self.directory.name)

    def tearDown(self):
        self.directory.cleanup()


class TestBackendCombo(OnlineUiCase):
    def test_the_bbhost_group_follows_the_combo(self):
        self.assertTrue(contains(self.window.page, self.window.bbhost_group))  # actually on the page
        self.assertTrue(contains(self.window.page, self.window.shadnet_login_row))
        self.assertTrue(self.window.bbhost_group.get_visible())
        self.assertFalse(self.window.bbhost_local.get_expanded())  # local account tucked away
        self.window.backend_row.set_selected(0)  # shadnet
        self.assertFalse(self.window.bbhost_group.get_visible())
        self.window.backend_row.set_selected(1)  # bbhost
        self.assertTrue(self.window.bbhost_group.get_visible())


class TestServerRow(OnlineUiCase):
    def test_the_resolved_base_is_shown_and_typos_are_refused(self):
        info, icon = self.window.bbhost_server_info, self.window.bbhost_server_icon
        self.assertEqual(info.get_title(), "https://thehuntersdream.com:18671")
        self.assertEqual(icon.get_icon_name(), "network-server-symbolic")
        self.window.bbhost_server_row.set_text("meu.lan")
        self.assertEqual(info.get_title(), "https://meu.lan:18671")
        self.window.bbhost_server_row.set_text("gopher://x")
        self.assertIn("Ошибка", info.get_title())
        self.assertEqual(icon.get_icon_name(), "dialog-warning-symbolic")
        self.assertIn("error", info.get_css_classes())
        self.window.bbhost_server_row.set_text("")  # back to the default: clean again
        self.assertEqual(icon.get_icon_name(), "network-server-symbolic")
        self.assertNotIn("error", info.get_css_classes())


class TestLinkFlow(OnlineUiCase):
    def test_a_network_failure_restores_everything(self):
        with mock.patch.object(bbhost, "device_start",
                               side_effect=bbhost.BbhostError("network: recusado")):
            self.window.on_bbhost_link(self.window.bbhost_link_button)
            pump()
        self.assertIn("network: recusado", self.window.bbhost_status.get_subtitle())
        self.assertIsNone(self.window.bbhost_link)
        self.assertEqual(self.window.bbhost_link_button.get_label(), "Ввязать")

    def test_approval_saves_the_account_and_greets_by_name(self):
        with mock.patch.object(bbhost, "device_start", lambda _server: dict(LINK)), \
                mock.patch.object(bbhost, "device_wait",
                                  lambda _s, _l, cancel=None, sleep=None:
                                  ("approved", "Hunter", "tok")):
            self.window.on_bbhost_link(self.window.bbhost_link_button)
            pump()
        self.assertEqual(bbhost.load_account(self.window.account_file), ("Hunter", "tok"))
        self.assertIn("Hunter", self.window.bbhost_status.get_title())
        self.assertEqual(self.window.bbhost_link_button.get_label(), "Сменить аккаунт")
        self.assertEqual(self.window.bbhost_status_icon.get_icon_name(), "emblem-ok-symbolic")
        self.assertTrue(any("Hunter" in toast.get_property("title")
                            for toast in self.window.toasts.items))
        self.assertIsNone(self.window.bbhost_link)

    def test_the_button_cancels_while_waiting_for_the_website(self):
        release = threading.Event()

        def wait(_server, _link, cancel=None, sleep=None):
            release.wait(5)
            return "cancelled", "", ""

        with mock.patch.object(bbhost, "device_start", lambda _server: dict(LINK)), \
                mock.patch.object(bbhost, "device_wait", wait):
            self.window.on_bbhost_link(self.window.bbhost_link_button)
            self.assertTrue(pump_until(lambda: self.window.bbhost_link_button.get_label()
                                       == "Отмена"))
            self.window.bbhost_link_button.emit("clicked")  # the second click gives up
            release.set()
            pump()
        self.assertEqual(self.window.bbhost_status.get_subtitle(), "Ожидание отменено")
        self.assertEqual(self.window.bbhost_link_button.get_label(), "Ввязать")


class TestCreateAndRecover(OnlineUiCase):
    def test_create_saves_the_token_and_shows_the_code_once(self):
        with mock.patch.object(bbhost, "account_create",
                               return_value=("Hunter", "tok", "RC-1")) as create:
            self.window.bbhost_name_row.set_text("Hunter")
            self.window.on_bbhost_create(self.window.bbhost_create_button)
            pump()
        create.assert_called_once_with("", "Hunter")
        self.assertEqual(self.window.bbhost_code_row.get_text(), "RC-1")
        self.assertTrue(self.window.bbhost_local.get_expanded())  # the new code is in there
        self.assertEqual(bbhost.load_account(self.window.account_file), ("Hunter", "tok"))
        self.assertIn("Hunter", self.window.bbhost_status.get_title())
        self.assertEqual(self.window.bbhost_link_button.get_label(), "Сменить аккаунт")
        self.assertTrue(any("RC-1" in body for _heading, body in self.window.alerts))
        self.assertTrue(self.window.bbhost_create_button.get_sensitive())

    def test_recover_uses_the_name_and_the_code_from_the_rows(self):
        with mock.patch.object(bbhost, "account_recover",
                               return_value=("Hunter", "tok2", "RC-2")) as recover:
            self.window.bbhost_name_row.set_text("Hunter")
            self.window.bbhost_code_row.set_text("RC-1")
            self.window.on_bbhost_recover(self.window.bbhost_recover_button)
            pump()
        recover.assert_called_once_with("", "Hunter", "RC-1")
        self.assertEqual(self.window.bbhost_code_row.get_text(), "RC-2")
        self.assertEqual(bbhost.load_account(self.window.account_file), ("Hunter", "tok2"))

    def test_empty_fields_are_refused_before_any_call(self):
        self.window.on_bbhost_create(self.window.bbhost_create_button)
        self.window.on_bbhost_recover(self.window.bbhost_recover_button)
        self.assertEqual(len(self.window.alerts), 2)
        self.assertEqual(self.window.toasts.items, [])
        self.window.bbhost_name_row.set_text("Hunter")
        self.window.on_bbhost_recover(self.window.bbhost_recover_button)
        self.assertEqual(len(self.window.alerts), 3)  # still no code in the row


class TestSigninChecks(OnlineUiCase):
    """The launcher answers "is this account right" itself, one button each."""

    def test_the_shadnet_check_reports_a_pass(self):
        with mock.patch.object(bbport_shadnet, "check_login",
                               return_value=(True, "NoError (0)")) as check:
            self.window.npid_row.set_text("SrShadowy")
            self.window.password_row.set_text("hunter2")
            self.window.shadnet_login_button.emit("clicked")
            pump()
        self.assertEqual(check.call_args.args[1:], ("SrShadowy", "hunter2"))
        self.assertEqual(self.window.shadnet_login_row.get_subtitle(),
                         "Пароль принят: сервер принял вход")
        self.assertEqual(self.window.shadnet_login_icon.get_icon_name(), "emblem-ok-symbolic")
        self.assertNotIn("error", self.window.shadnet_login_row.get_css_classes())
        self.assertTrue(self.window.shadnet_login_button.get_sensitive())

    def test_a_refused_password_is_named_with_the_error_class(self):
        with mock.patch.object(bbport_shadnet, "check_login",
                               return_value=(False, "LoginInvalidPassword (8)")):
            self.window.npid_row.set_text("SrShadowy")
            self.window.password_row.set_text("wrong")
            self.window.shadnet_login_button.emit("clicked")
            pump()
        self.assertEqual(self.window.shadnet_login_row.get_subtitle(),
                         "Сервер отклонил: LoginInvalidPassword (8)")
        self.assertIn("error", self.window.shadnet_login_row.get_css_classes())
        self.assertEqual(self.window.shadnet_login_icon.get_icon_name(), "dialog-warning-symbolic")

    def test_empty_credentials_never_reach_the_network(self):
        with mock.patch.object(bbport_shadnet, "check_login") as check:
            self.window.npid_row.set_text("")
            self.window.password_row.set_text("")
            self.window.shadnet_login_button.emit("clicked")
            pump()
        check.assert_not_called()
        self.assertEqual(self.window.shadnet_login_row.get_subtitle(),
                         "Введите Online ID и пароль")

    def test_a_transport_error_reads_as_an_error(self):
        with mock.patch.object(bbport_shadnet, "check_login",
                               side_effect=bbport_shadnet.ShadnetError("network: saindo")):
            self.window.npid_row.set_text("x")
            self.window.password_row.set_text("y")
            self.window.shadnet_login_button.emit("clicked")
            pump()
        self.assertEqual(self.window.shadnet_login_row.get_subtitle(), "Ошибка: network: saindo")
        self.assertTrue(self.window.shadnet_login_button.get_sensitive())

    def test_the_dream_check_needs_a_linked_account(self):
        with mock.patch.object(bbhost, "account_check") as check:
            self.window.bbhost_verify_button.emit("clicked")
            pump()
        check.assert_not_called()
        self.assertEqual(self.window.bbhost_verify_row.get_subtitle(), "Аккаунт не привязан")

    def test_a_linked_token_is_checked_and_greeted(self):
        bbhost.save_account(self.window.account_file, "SrDream", "tok")
        with mock.patch.object(bbhost, "account_check") as check:
            self.window.bbhost_verify_button.emit("clicked")
            pump()
        check.assert_called_once_with("", "tok")
        self.assertEqual(self.window.bbhost_verify_row.get_subtitle(), "Токен принят: SrDream")
        self.assertNotIn("error", self.window.bbhost_verify_row.get_css_classes())
        self.assertTrue(self.window.bbhost_verify_button.get_sensitive())

    def test_a_refused_token_carries_the_server_reason(self):
        bbhost.save_account(self.window.account_file, "SrDream", "tok")
        with mock.patch.object(bbhost, "account_check",
                               side_effect=bbhost.BbhostError("token expired")):
            self.window.bbhost_verify_button.emit("clicked")
            pump()
        self.assertEqual(self.window.bbhost_verify_row.get_subtitle(), "Ошибка: token expired")
        self.assertIn("error", self.window.bbhost_verify_row.get_css_classes())


if __name__ == "__main__":
    unittest.main()
