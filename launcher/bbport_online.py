# SPDX-License-Identifier: GPL-2.0-or-later
"""The launcher's Online page: shadNet coop plus the account of the selected
service (plan ONLINE_BBHOST_PLAN §3). The "Сервис онлайн" combo picks shadNet
(public shadPS4 server) or bbhost; the bbhost account group lives in
bbport_bbhost_ui.py (BbhostAccountMixin). The shadNet rows keep the coop path
in both modes until the session switch lands in Fase 2.
"""

import threading

import bbport_shadnet
import online

from bbport_config import online_module
from bbport_i18n import tr
from bbport_ui_helpers import combo_row, combo_value

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, GLib, Gtk  # noqa: E402


class OnlineMixin:
    """Mixin for LauncherWindow handling the Online page."""

    def build_online_page(self):
        s = self.settings
        page = Adw.PreferencesPage()
        play = Adw.PreferencesGroup(description=tr(
            "Сообщения, пятна крови и призраки приходят с The Hunter's Dream; колокола и призывы "
            "идут через сервер shadNet. Игра по сети совместима с версией для Windows: нужны версия "
            "игры 1.09 и тот же сервер"))
        self.online_row = Adw.SwitchRow(title=tr("Играть онлайн"),
                                        subtitle=tr("Выключено: игра остаётся офлайн, как раньше"),
                                        active=s["online"])
        play.add(self.online_row)
        if not online_module():
            self.online_row.set_sensitive(False)
            self.online_row.set_subtitle(tr("Эта сборка без модуля онлайна (libbbnet.so): игра только офлайн"))
        self.backend_row = combo_row(
            tr("Сервис онлайн"), None,
            [("shadNet (srv.shadps4.net)", "shadnet",
              "Колокола, призывы и пароль сессии — через публичный сервер shadPS4"),
             ("The Hunter's Dream (bbhost)", "bbhost",
              "Аккаунт The Hunter's Dream; кооп-бэкенд подключается следующим этапом")],
            s["online_backend"])
        play.add(self.backend_row)
        page.add(play)

        self.bbhost_group = self.build_bbhost_group(s)
        page.add(self.bbhost_group)
        self.backend_row.connect("notify::selected", self.on_backend_changed)
        self.on_backend_changed()

        game = Adw.PreferencesGroup(title=tr("The Hunter's Dream: сообщения, пятна крови и призраки"))
        self.community_row = Adw.EntryRow(title=tr("Игровой сервер"), text=s["online_community"])
        game.add(self.community_row)
        page.add(game)

        shadnet = Adw.PreferencesGroup(
            title=tr("shadNet: колокола, призывы и аккаунт"),
            description=tr("Публичный сервер shadPS4: через него идут колокола и призывы. Имя "
                           "учётной записи — не email: регистрация на shadnet.shadps4.net или "
                           "у владельца частного сервера (его host:port — в строке адреса). "
                           "WebAPI: пусто = адрес сервера с портом 31315. Пароль хранится "
                           "вместе с настройками лаунчера"))
        self.npid_row = Adw.EntryRow(title=tr("Online ID (NPID)"), text=s["online_npid"])
        self.password_row = Adw.PasswordEntryRow(title=tr("Пароль"), text=s["online_password"])
        self.server_row = Adw.EntryRow(title=tr("Адрес сервера"), text=s["online_server"])
        self.webapi_row = Adw.EntryRow(title=tr("Адрес WebAPI"), text=s["online_webapi"])
        login = Adw.ActionRow(title=tr("Вход в shadNet (NPID и пароль)"))
        self.shadnet_login_icon = Gtk.Image(icon_name="dialog-information-symbolic")
        login.add_prefix(self.shadnet_login_icon)
        self.shadnet_login_button = Gtk.Button(label=tr("Проверить вход"), valign=Gtk.Align.CENTER)
        self.shadnet_login_button.connect("clicked", self.on_shadnet_login)
        login.add_suffix(self.shadnet_login_button)
        self.shadnet_login_row = login
        for row in (self.npid_row, self.password_row, self.server_row, self.webapi_row, login):
            shadnet.add(row)
        page.add(shadnet)

        connection = Adw.PreferencesGroup(
            title=tr("Соединение"),
            description=tr("У всех в сессии должны быть одна версия игры (1.09) и один сервер. "
                           "Читы меняют игру и для других игроков: выключайте их при совместной игре"))
        self.upnp_row = Adw.SwitchRow(title=tr("UPnP (открыть порт на роутере автоматически)"),
                                      subtitle=tr("Выключите при Tailscale или другом VPN"),
                                      active=s["online_upnp"])
        connection.add(self.upnp_row)
        test = Adw.ActionRow(title=tr("Проверить соединение"))
        self.test_button = Gtk.Button(label=tr("Проверить"), valign=Gtk.Align.CENTER)
        self.test_button.connect("clicked", self.on_test_connection)
        test.add_suffix(self.test_button)
        connection.add(test)
        self.test_rows = []
        self.connection_group = connection
        page.add(connection)
        return page

    def on_backend_changed(self, *_):
        """Only the selected service's account group is in the way."""
        self.bbhost_group.set_visible(combo_value(self.backend_row) == "bbhost")

    def online_addresses(self):
        server = self.server_row.get_text().strip() or online.SHADNET_SERVER
        community = online.community_address(self.community_row.get_text())
        webapi = online.webapi_setting(self.webapi_row.get_text(), server, community)
        return server, webapi, community

    def on_test_connection(self, _button):
        server, webapi, community = self.online_addresses()
        names = {"game": tr("Игровой сервер"), "shadnet": tr("Сервер shadNet"), "webapi": "WebAPI"}
        for row in self.test_rows:
            self.connection_group.remove(row)
        self.test_rows = []
        self.test_button.set_sensitive(False)
        self.test_button.set_label(tr("Проверка…"))

        def show(results):
            for name, address, ok, detail in results:
                row = Adw.ActionRow(title=GLib.markup_escape_text(f"{names[name]}: {address}"),
                                    subtitle=GLib.markup_escape_text(
                                        tr("доступен") if ok else f'{tr("недоступен")} ({detail})'))
                row.add_prefix(Gtk.Image(icon_name="object-select-symbolic" if ok else "dialog-warning-symbolic"))
                self.connection_group.add(row)
                self.test_rows.append(row)
            self.test_button.set_sensitive(True)
            self.test_button.set_label(tr("Проверить"))
            return False

        def work():
            try:
                results = online.check(server, webapi, community)
            except Exception as error:  # a probe the code did not foresee: say it, never hang
                results = [(name, address, False, str(error)) for name, address in
                           (("game", community), ("shadnet", server), ("webapi", webapi))]
            GLib.idle_add(show, results)
        threading.Thread(target=work, daemon=True).start()

    def on_shadnet_login(self, _button):
        """One Login to the configured shadNet server: the verdict the game's
        log would only show once, after a launch."""
        server = self.server_row.get_text().strip() or online.SHADNET_SERVER
        npid = self.npid_row.get_text().strip()
        password = self.password_row.get_text()
        if not npid or not password:
            self.set_login_result(tr("Введите Online ID и пароль"),
                                  "dialog-warning-symbolic", False)
            return
        self.shadnet_login_button.set_sensitive(False)
        self.set_login_result(tr("Проверка…"), "dialog-information-symbolic", True)

        def done(text, ok):
            self.shadnet_login_button.set_sensitive(True)
            self.set_login_result(text, "emblem-ok-symbolic" if ok else "dialog-warning-symbolic",
                                  ok)
            return False

        def work():
            try:
                ok, verdict = bbport_shadnet.check_login(server, npid, password)
            except bbport_shadnet.ShadnetError as error:
                GLib.idle_add(done, tr("Ошибка: {}").format(error), False)
                return
            message = (tr("Пароль принят: сервер принял вход") if ok
                       else tr("Сервер отклонил: {}").format(verdict))
            GLib.idle_add(done, message, ok)

        threading.Thread(target=work, daemon=True).start()

    def set_login_result(self, text, icon, ok):
        """The sign-in check's verdict row: the words, the picture, the class."""
        self.shadnet_login_row.set_subtitle(text)
        self.shadnet_login_icon.set_from_icon_name(icon)
        if ok:
            self.shadnet_login_row.remove_css_class("error")
        else:
            self.shadnet_login_row.add_css_class("error")
