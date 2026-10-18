# SPDX-License-Identifier: GPL-2.0-or-later
"""UI tests for the Online mixin's Hunter's Dream / hosts account group."""
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

import bbport_hosts as hosts  # noqa: E402
import bbport_config  # noqa: E402
import bbport_shadnet  # noqa: E402
from bbport_online import OnlineMixin  # noqa: E402
import bbport_hosts_ui  # noqa: E402
from bbport_hosts_ui import HostsAccountMixin  # noqa: E402

Adw.init()

LINK = {"DeviceCode": "dc", "UserCode": "ABCD-1234",
        "VerificationUri": "https://example.com/link", "Interval": 5, "ExpiresIn": 600}


class _Toasts:
    def __init__(self):
        self.items = []

    def add_toast(self, toast):
        self.items.append(toast)


class FakeWindow(OnlineMixin, HostsAccountMixin):
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
    loop = GLib.MainLoop()
    GLib.timeout_add(int(seconds * 1000), loop.quit)
    loop.run()


def contains(parent, widget):
    child = parent.get_first_child()
    while child:
        if child is widget or contains(child, widget):
            return True
        child = child.get_next_sibling()
    return False


def pump_until(check, seconds=5.0):
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
        for patch in (mock.patch.object(bbport_hosts_ui, "ACCOUNT_FILE",
                                        Path(self.directory.name) / "account.json"),
                      mock.patch.object(bbport_hosts_ui.Gio.AppInfo,
                                        "launch_default_for_uri", lambda *args: None)):
            patch.start()
            self.addCleanup(patch.stop)
        self.window = FakeWindow(self.directory.name)

    def tearDown(self):
        self.directory.cleanup()


class TestBackendCombo(OnlineUiCase):
    def test_the_bbhost_group_follows_the_combo(self):
        self.assertTrue(contains(self.window.page, self.window.bbhost_group))
        self.assertTrue(contains(self.window.page, self.window.shadnet_login_row))
        self.assertTrue(self.window.bbhost_group.get_visible())
        self.assertFalse(self.window.bbhost_local.get_expanded())
        self.window.backend_row.set_selected(0)
        self.assertFalse(self.window.bbhost_group.get_visible())
        self.window.backend_row.set_selected(1)
        self.assertTrue(self.window.bbhost_group.get_visible())


class TestServerRow(OnlineUiCase):
    def test_the_resolved_base_is_shown_and_typos_are_refused(self):
        info, icon = self.window.bbhost_server_info, self.window.bbhost_server_icon
        self.assertEqual(info.get_title(), "https://thehuntersdream.com:18671")
        self.assertEqual(icon.get_icon_name(), "network-server-symbolic")
        self.window.bbhost_server_row.set_text("meu.lan")
        self.assertEqual(info.get_title(), "https://meu.lan:18671")
        self.window.bbhost_server_row.set_text("bad://meu.lan")
        self.assertTrue(info.has_css_class("error"))
        self.assertEqual(icon.get_icon_name(), "dialog-warning-symbolic")
        self.window.bbhost_server_row.set_text("")
        self.assertFalse(info.has_css_class("error"))


class TestLinkFlow(OnlineUiCase):
    def test_a_network_failure_restores_everything(self):
        with mock.patch.object(hosts, "device_start",
                               side_effect=hosts.HostsError("network: recusado")):
            self.window.on_bbhost_link(self.window.bbhost_link_button)
            pump()
        self.assertIn("network: recusado", self.window.bbhost_status.get_subtitle())
        self.assertEqual(self.window.bbhost_link_button.get_label(), "Ввязать")

    def test_approval_saves_the_account_and_greets_by_name(self):
        with mock.patch.object(hosts, "device_start", return_value=LINK), \
                mock.patch.object(hosts, "device_wait",
                                  return_value=("approved", "Hunter", "tok")):
            self.window.on_bbhost_link(self.window.bbhost_link_button)
            pump()
        self.assertEqual(hosts.load_account(self.window.account_file), ("Hunter", "tok"))
        self.assertIn("Hunter", self.window.bbhost_status.get_title())
        self.assertEqual(self.window.bbhost_link_button.get_label(), "Сменить аккаунт")

    def test_the_button_cancels_while_waiting_for_the_website(self):
        release = threading.Event()

        def wait(_server, _link, cancel=None, sleep=None):
            release.wait(5)
            return "cancelled", "", ""

        with mock.patch.object(hosts, "device_start", lambda _server: dict(LINK)), \
                mock.patch.object(hosts, "device_wait", wait):
            self.window.on_bbhost_link(self.window.bbhost_link_button)
            self.assertTrue(pump_until(lambda: self.window.bbhost_link_button.get_label()
                                       == "Отмена"))
            self.window.bbhost_link_button.emit("clicked")
            release.set()
            pump()
        self.assertEqual(self.window.bbhost_status.get_subtitle(), "Ожидание отменено")
        self.assertEqual(self.window.bbhost_link_button.get_label(), "Ввязать")


class TestCreateAndRecover(OnlineUiCase):
    def test_create_saves_the_token_and_shows_the_code_once(self):
        with mock.patch.object(hosts, "account_create",
                               return_value=("Hunter", "tok", "RC-1")) as create:
            self.window.bbhost_name_row.set_text("Hunter")
            self.window.on_bbhost_create(self.window.bbhost_create_button)
            pump()
        create.assert_called_once_with("", "Hunter")
        self.assertEqual(self.window.bbhost_code_row.get_text(), "RC-1")
        self.assertTrue(self.window.bbhost_local.get_expanded())
        self.assertEqual(hosts.load_account(self.window.account_file), ("Hunter", "tok"))
        self.assertIn("Hunter", self.window.bbhost_status.get_title())

    def test_recover_uses_the_name_and_the_code_from_the_rows(self):
        with mock.patch.object(hosts, "account_recover",
                               return_value=("Hunter", "tok2", "RC-2")) as recover:
            self.window.bbhost_name_row.set_text("Hunter")
            self.window.bbhost_code_row.set_text("RC-1")
            self.window.on_bbhost_recover(self.window.bbhost_recover_button)
            pump()
        recover.assert_called_once_with("", "Hunter", "RC-1")
        self.assertEqual(self.window.bbhost_code_row.get_text(), "RC-2")
        self.assertEqual(hosts.load_account(self.window.account_file), ("Hunter", "tok2"))


class TestSigninChecks(OnlineUiCase):
    def test_the_dream_check_needs_a_linked_account(self):
        self.window.on_bbhost_verify(self.window.bbhost_verify_button)
        self.assertEqual(self.window.bbhost_verify_row.get_subtitle(), "Аккаунт не привязан")

    def test_a_linked_token_is_checked_and_greeted(self):
        hosts.save_account(self.window.account_file, "Hunter", "tok")
        self.window.update_bbhost_status()
        with mock.patch.object(hosts, "account_check", return_value=True) as check:
            self.window.on_bbhost_verify(self.window.bbhost_verify_button)
            pump()
        check.assert_called_once_with("", "tok")
        self.assertEqual(self.window.bbhost_verify_row.get_subtitle(), "Токен принят: Hunter")


if __name__ == "__main__":
    unittest.main()
