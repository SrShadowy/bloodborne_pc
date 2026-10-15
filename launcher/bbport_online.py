# SPDX-License-Identifier: GPL-2.0-or-later
"""The launcher's Online page: shadNet coop plus the account of the selected
service (plan ONLINE_BBHOST_PLAN §3). The "Сервис онлайн" combo picks shadNet
(public shadPS4 server) or bbhost; the bbhost account group lives in
bbport_bbhost_ui.py (BbhostAccountMixin). The shadNet rows keep the coop path
in both modes until the session switch lands in Fase 2.
"""

import threading

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

        game = Adw.PreferencesGroup(title=tr("Игровой сервер (сообщения, пятна крови, призраки)"))
        self.community_row = Adw.EntryRow(title=tr("Игровой сервер"), text=s["online_community"])
        game.add(self.community_row)
        page.add(game)

        coop = Adw.PreferencesGroup(
            title=tr("Сервер кооператива (колокола и призывы)"),
            description=tr("srv.shadps4.net:31313 — публичный сервер shadPS4. Для частного сервера "
                           "укажите host:port от его владельца. WebAPI: оставьте пустым, тогда "
                           "берётся адрес сервера с портом 31315"))
        self.server_row = Adw.EntryRow(title=tr("Адрес сервера"), text=s["online_server"])
        self.webapi_row = Adw.EntryRow(title=tr("Адрес WebAPI"), text=s["online_webapi"])
        coop.add(self.server_row)
        coop.add(self.webapi_row)
        page.add(coop)

        account = Adw.PreferencesGroup(
            title=tr("Учётная запись shadNet"),
            description=tr("Имя учётной записи shadNet, не email. Зарегистрируйтесь на "
                           "shadnet.shadps4.net или спросите владельца частного сервера. Пароль "
                           "хранится на этом компьютере вместе с настройками лаунчера"))
        self.npid_row = Adw.EntryRow(title=tr("Online ID (NPID)"), text=s["online_npid"])
        self.password_row = Adw.PasswordEntryRow(title=tr("Пароль"), text=s["online_password"])
        account.add(self.npid_row)
        account.add(self.password_row)
        page.add(account)

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
