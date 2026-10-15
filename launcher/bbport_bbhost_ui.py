# SPDX-License-Identifier: GPL-2.0-or-later
"""The Hunter's Dream account group of the Online page (bbport_bbhost client).

Split from bbport_online.py (one concern per file): the page owns the layout,
this mixin owns the account flow's widgets and threads. The device link
(website + Discord sign-in) is the primary path — one button on the status
row; creating or recovering a local account hides behind an expander so the
common case stays a single click. The shadNet coop rows remain in charge of
the session until the backend switch lands (Fase 2 of ONLINE_BBHOST_PLAN).
"""

import threading

import bbport_bbhost as bbhost
from bbport_config import ACCOUNT_FILE
from bbport_i18n import tr

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, Gio, GLib, Gtk  # noqa: E402


class BbhostAccountMixin:
    """Mixin for LauncherWindow handling the bbhost account group."""

    def build_bbhost_group(self, s):
        """The group's widgets; the caller adds it to the Online page."""
        self.bbhost_link = None  # a threading.Event while a device link waits
        group = Adw.PreferencesGroup(
            title=tr("Учётная запись The Hunter's Dream"),
            description=tr("Без паролей: код на сайте (вход через Discord) привязывает этот "
                           "компьютер, либо локальный аккаунт с кодом восстановления. Токен "
                           "хранится в настройках лаунчера. Колокола и призывы пока идут через "
                           "shadNet — строки ниже."))
        self.bbhost_status = Adw.ActionRow(title=tr("Аккаунт не привязан"))
        self.bbhost_status_icon = Gtk.Image(icon_name="avatar-default-symbolic")
        self.bbhost_status.add_prefix(self.bbhost_status_icon)
        self.bbhost_spinner = Gtk.Spinner(width_request=16, height_request=16)
        self.bbhost_status.add_suffix(self.bbhost_spinner)
        self.bbhost_link_button = Gtk.Button(label=tr("Ввязать"), valign=Gtk.Align.CENTER)
        self.bbhost_link_button.connect("clicked", self.on_bbhost_link)
        self.bbhost_status.add_suffix(self.bbhost_link_button)
        group.add(self.bbhost_status)
        self.bbhost_verify_row = Adw.ActionRow(title=tr("Вход в The Hunter's Dream (токен)"))
        self.bbhost_verify_icon = Gtk.Image(icon_name="dialog-information-symbolic")
        self.bbhost_verify_row.add_prefix(self.bbhost_verify_icon)
        self.bbhost_verify_button = Gtk.Button(label=tr("Проверить вход"), valign=Gtk.Align.CENTER)
        self.bbhost_verify_button.connect("clicked", self.on_bbhost_verify)
        self.bbhost_verify_row.add_suffix(self.bbhost_verify_button)
        group.add(self.bbhost_verify_row)
        self.bbhost_server_row = Adw.EntryRow(title=tr("Сервер аккаунтов"),
                                              text=s["online_bbhost_server"])
        self.bbhost_server_row.connect("changed", self.on_bbhost_server_changed)
        self.bbhost_server_info = Adw.ActionRow()  # EntryRow has no subtitle: the base goes here
        self.bbhost_server_icon = Gtk.Image(icon_name="network-server-symbolic")
        self.bbhost_server_info.add_prefix(self.bbhost_server_icon)
        group.add(self.bbhost_server_row)
        group.add(self.bbhost_server_info)
        local = Adw.ExpanderRow(
            title=tr("Создать или восстановить аккаунт"),
            subtitle=tr("Создание — имя. Восстановление — имя и код"))
        self.bbhost_name_row = Adw.EntryRow(title=tr("Имя аккаунта"))
        self.bbhost_code_row = Adw.EntryRow(title=tr("Код восстановления"))
        sign = Adw.ActionRow()
        self.bbhost_create_button = Gtk.Button(label=tr("Создать"), valign=Gtk.Align.CENTER)
        self.bbhost_create_button.connect("clicked", self.on_bbhost_create)
        self.bbhost_recover_button = Gtk.Button(label=tr("Восстановить"), valign=Gtk.Align.CENTER)
        self.bbhost_recover_button.connect("clicked", self.on_bbhost_recover)
        sign.add_suffix(self.bbhost_create_button)
        sign.add_suffix(self.bbhost_recover_button)
        for row in (self.bbhost_name_row, self.bbhost_code_row, sign):
            local.add_row(row)
        group.add(local)
        self.bbhost_local = local
        self.update_bbhost_status()
        self.on_bbhost_server_changed(self.bbhost_server_row)
        return group

    def on_bbhost_server_changed(self, row):
        """Resolve the server into the row below: a scheme typo shows here, not at launch."""
        try:
            base = bbhost.server_base(row.get_text())
        except bbhost.BbhostError as error:
            self.bbhost_server_icon.set_from_icon_name("dialog-warning-symbolic")
            self.bbhost_server_info.add_css_class("error")
            self.bbhost_server_info.set_title(tr("Ошибка: {}").format(error))
            return
        self.bbhost_server_icon.set_from_icon_name("network-server-symbolic")
        self.bbhost_server_info.remove_css_class("error")
        self.bbhost_server_info.set_title(base)

    def update_bbhost_status(self):
        """Who is linked, and the button the flow starts from ("change account"
        once there is one to replace). A new sign-in makes the old check's
        verdict stale, so the verify row goes back to neutral."""
        online_id, _token = bbhost.load_account(ACCOUNT_FILE)
        self.bbhost_status.set_title(
            tr("Привязано: {}").format(online_id) if online_id else tr("Аккаунт не привязан"))
        self.bbhost_status_icon.set_from_icon_name(
            "emblem-ok-symbolic" if online_id else "avatar-default-symbolic")
        self.bbhost_link_button.set_label(tr("Сменить аккаунт") if online_id else tr("Ввязать"))
        self.set_verify_result("", "dialog-information-symbolic", True)

    def set_verify_result(self, text, icon, ok):
        """The check's verdict row: the words, the picture and the error class."""
        self.bbhost_verify_row.set_subtitle(text)
        self.bbhost_verify_icon.set_from_icon_name(icon)
        if ok:
            self.bbhost_verify_row.remove_css_class("error")
        else:
            self.bbhost_verify_row.add_css_class("error")

    def on_bbhost_verify(self, _button):
        """Ask the account server whether this PC's token still signs in — the
        only place the answer exists: the game has no password to re-check."""
        online_id, token = bbhost.load_account(ACCOUNT_FILE)
        if not token:
            self.set_verify_result(tr("Аккаунт не привязан"),
                                   "avatar-default-symbolic", False)
            return
        server = self.bbhost_server_row.get_text()
        self.bbhost_verify_button.set_sensitive(False)
        self.set_verify_result(tr("Проверка…"), "dialog-information-symbolic", True)

        def done(text, ok):
            self.bbhost_verify_button.set_sensitive(True)
            self.set_verify_result(
                text, "emblem-ok-symbolic" if ok else "dialog-warning-symbolic", ok)
            return False

        def work():
            try:
                bbhost.account_check(server, token)
            except bbhost.BbhostError as error:
                GLib.idle_add(done, tr("Ошибка: {}").format(error), False)
                return
            GLib.idle_add(done, tr("Токен принят: {}").format(online_id), True)

        threading.Thread(target=work, daemon=True).start()

    def set_account_buttons(self, link, sign):
        """One account call at a time; while it waits, the link button cancels."""
        self.bbhost_link_button.set_sensitive(link)
        self.bbhost_create_button.set_sensitive(sign)
        self.bbhost_recover_button.set_sensitive(sign)

    def on_bbhost_link(self, _button):
        if self.bbhost_link is not None:  # waiting for approval: this click gives up
            self.bbhost_link.set()
            return
        server = self.bbhost_server_row.get_text()
        cancel = threading.Event()
        self.bbhost_link = cancel
        self.set_account_buttons(True, False)
        self.bbhost_spinner.start()
        self.bbhost_spinner.set_visible(True)
        self.bbhost_status.set_subtitle(tr("Запрос кода…"))

        def finish(subtitle):
            self.bbhost_link = None
            self.bbhost_spinner.stop()
            self.bbhost_spinner.set_visible(False)
            self.update_bbhost_status()  # title, icon and label back to the linked state
            self.set_account_buttons(True, True)
            self.bbhost_status.set_subtitle(subtitle)
            return False

        def approved(link):
            self.bbhost_link_button.set_label(tr("Отмена"))
            self.bbhost_status.set_subtitle(
                tr("Код {} — подтвердите на {}").format(link["UserCode"], link["VerificationUri"]))
            uri = link["VerificationUri"] + "?code=" + link["UserCode"]
            try:
                Gio.AppInfo.launch_default_for_uri(uri, None)
            except GLib.Error as error:  # no browser: the code is still on screen
                self.toasts.add_toast(Adw.Toast(title=tr("Браузер не открылся: {}").format(error)))

        def signed_in(status, online_id, token):
            if status == "approved":
                bbhost.save_account(ACCOUNT_FILE, online_id, token)
                self.toasts.add_toast(Adw.Toast(title=tr("Аккаунт привязан: {}").format(online_id)))
                return finish("")
            messages = {"denied": tr("Ссылка отклонена на сайте"),
                        "expired": tr("Код истёк: запросите новый"),
                        "cancelled": tr("Ожидание отменено")}
            return finish(messages.get(status, status))

        def work():
            try:
                link = bbhost.device_start(server)
            except bbhost.BbhostError as error:
                GLib.idle_add(finish, tr("Ошибка: {}").format(error))
                return
            if cancel.is_set():  # given up while the code was being requested
                GLib.idle_add(finish, tr("Ожидание отменено"))
                return
            GLib.idle_add(approved, link)
            try:
                status, online_id, token = bbhost.device_wait(server, link, cancel=cancel)
            except bbhost.BbhostError as error:
                GLib.idle_add(finish, tr("Ошибка: {}").format(error))
                return
            GLib.idle_add(signed_in, status, online_id, token)

        threading.Thread(target=work, daemon=True).start()

    def on_bbhost_create(self, _button):
        name = self.bbhost_name_row.get_text().strip()
        if not name:
            self.alert(tr("Ввести имя аккаунта"), tr("Живёт на сервере: это не email и не пароль"))
            return
        self.run_bbhost_sign(
            lambda server: bbhost.account_create(server, name),
            tr("Аккаунт {}: запишите код восстановления — он больше не показывается. {}"))

    def on_bbhost_recover(self, _button):
        name = self.bbhost_name_row.get_text().strip()
        code = self.bbhost_code_row.get_text().strip()
        if not name:
            self.alert(tr("Ввести имя аккаунта"), tr("Живёт на сервере: это не email и не пароль"))
            return
        if not code:
            self.alert(tr("Ввести код восстановления"), tr("Код выдал сервер при создании аккаунта"))
            return
        self.run_bbhost_sign(
            lambda server: bbhost.account_recover(server, name, code),
            tr("Аккаунт {}: новый код восстановления — запишите. {}"))

    def run_bbhost_sign(self, call, message):
        """Create or recover in a thread: save the token, show the code once."""
        server = self.bbhost_server_row.get_text()
        self.set_account_buttons(False, False)

        def done(result, error):
            self.set_account_buttons(True, True)
            if error:
                self.toasts.add_toast(Adw.Toast(title=tr("Ошибка: {}").format(error)))
                return
            online_id, token, code = result
            bbhost.save_account(ACCOUNT_FILE, online_id, token)
            self.bbhost_code_row.set_text(code)
            self.update_bbhost_status()
            self.bbhost_local.set_expanded(True)  # the new code is in there
            self.alert(tr("Код восстановления"), message.format(online_id, code))
            return False

        def work():
            try:
                result = call(server)
            except bbhost.BbhostError as error:
                GLib.idle_add(done, None, str(error))
                return
            GLib.idle_add(done, result, None)

        threading.Thread(target=work, daemon=True).start()
