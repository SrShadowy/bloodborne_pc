#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bloodborne Seamless Hunt - Radar de Mundos Online e Monitor de Conexões."""

import argparse
import base64
import json
import time
import urllib.request

DEFAULT_SERVER = "http://play.seamlesshunt.com:31315"

AREA_MAP = {
    240000: "Sonho do Caçador", 240010: "Sonho (Oficina)", 240030: "Sonho (Jardim)",
    241000: "Central Yharnam (Clínica)", 241010: "Central Yharnam (Lâmpada)",
    241020: "Central Yharnam (Ponte)", 241030: "Túmulo de Oedon (Gascoigne)",
    242000: "Distrito da Catedral", 242010: "Grande Catedral (Amelia)",
    243000: "Yharnam Antiga", 243010: "Igreja do Bom Cálice",
    244000: "Ossuário de Hemwick", 245000: "Bosques Proibidos",
    246000: "Byrgenwerth (Rom)", 247000: "Yahar'gul (Vila Invisível)",
    248000: "Catedral Superior", 249000: "Castelo de Cainhurst",
    250000: "Fronteira do Pesadelo", 251000: "Pesadelo de Mensis",
    270000: "Pesadelo do Caçador (DLC)", 280040: "Aldeia dos Pescadores (DLC)",
}


def get_area_name(area_id):
    if not isinstance(area_id, int):
        return str(area_id)
    if area_id in AREA_MAP:
        return AREA_MAP[area_id]
    prefix = (area_id // 1000) * 1000
    if prefix in AREA_MAP:
        return f"{AREA_MAP[prefix]} (+{area_id % 1000})"
    if 260000 <= area_id < 270000:
        return f"Masmorra do Cálice ({area_id})"
    return f"Região {area_id}"


def _query(endpoint, payload, timeout=4):
    req = urllib.request.Request(
        endpoint, data=payload, headers={"Content-Type": "application/json"}, method="POST"
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8", errors="replace"))


def fetch_summon_signs(server_url=DEFAULT_SERVER, password=None, timeout=4):
    endpoint = server_url.rstrip("/") + "/summon_messenger/get"
    raw_signs, seen = [], set()
    try:
        queries = [b"{}"]
        if password:
            queries.append(json.dumps({"SummonWord": password}).encode("utf-8"))
        for q in queries:
            for s in _query(endpoint, q, timeout).get("SummonDataList", []):
                k = (s.get("UserId"), s.get("SessionId"))
                if k not in seen:
                    seen.add(k)
                    raw_signs.append(s)

        parsed = []
        for s in raw_signs:
            raw = s.get("SummonData", "")
            name = "Desconhecido"
            if raw:
                try:
                    b = base64.b64decode(raw)
                    if len(b) >= 0x50:
                        name = b[0x40:0x50].split(b"\x00")[0].decode("utf-8", errors="replace")
                except Exception:
                    pass
            parsed.append({
                "name": name, "level": s.get("MatchingLevel", "?"),
                "area_name": get_area_name(s.get("AreaRegionId", 0)),
                "password": s.get("SummonWord"),
                "user_id": s.get("UserId", "?"), "session_id": s.get("SessionId", "?"),
            })
        return True, parsed, ""
    except Exception as exc:
        return False, [], str(exc)


class SignTracker:
    def __init__(self):
        self.active = {}
        self.events = []

    def update(self, signs):
        curr = {(s["user_id"], s["session_id"]): s for s in signs}
        now = time.strftime("%H:%M:%S")
        for k, s in curr.items():
            if k not in self.active:
                pwd = f"senha '{s['password']}'" if s["password"] else "público"
                self.events.append((now, f"🔔 Mundo Aberto: {s['name']} (Lv {s['level']}) em {s['area_name']} [{pwd}]", "cyan"))
        for k, s in self.active.items():
            if k not in curr:
                self.events.append((now, f"🤝 CONEXÃO! O mundo de {s['name']} foi reivindicado por um visitante!", "green"))
        self.active = curr
        if len(self.events) > 20:
            self.events = self.events[-20:]
        return self.events


def run_terminal(server_url, password=None, interval=3):
    from rich.console import Console
    from rich.live import Live
    from rich.panel import Panel
    from rich.table import Table

    console = Console()
    tracker = SignTracker()

    def build_view():
        ok, signs, err = fetch_summon_signs(server_url, password)
        events = tracker.update(signs if ok else [])
        now = time.strftime("%H:%M:%S")

        table = Table(title=f"Marcas de Convocação Disponíveis - Kin Bell ({len(signs)})", expand=True)
        table.add_column("Caçador Disponível (Kin Bell)", style="bold cyan")
        table.add_column("Nível", justify="right", style="magenta")
        table.add_column("Área / Lâmpada", style="yellow")
        table.add_column("Senha de Rede", style="bold")
        table.add_column("Função", style="green")

        if not ok:
            table.add_row("[red]Falha ao conectar[/red]", "-", f"[red]{err}[/red]", "-", "-")
        elif not signs:
            table.add_row("[dim]Nenhum sinal de Kin Bell no momento[/dim]", "-", "-", "-", "-")
        else:
            for s in signs:
                pwd = s["password"]
                p_text = f"[bold green]\"{pwd}\"[/bold green]" if pwd else "[dim red][Sem Senha][/dim red]"
                table.add_row(s["name"], str(s["level"]), s["area_name"], p_text, "Pronto para ser invocado")

        ev_table = Table(title="Feed de Conexões e Entradas ao Vivo", expand=True)
        ev_table.add_column("Hora", style="dim", width=10)
        ev_table.add_column("Evento", style="bold")
        if not events:
            ev_table.add_row("-", "[dim]Aguardando toques de sino ou conexões...[/dim]")
        else:
            for t_str, text, color in reversed(events[-6:]):
                ev_table.add_row(t_str, f"[{color}]{text}[/{color}]")

        info_pwd = f" | Senha: [bold green]\"{password}\"[/bold green]" if password else ""
        header = f"[bold red]Bloodborne[/bold red] Radar & Conexões - [underline]{server_url}[/underline]{info_pwd} | {now}"
        return Panel(table, title=header, subtitle="Ctrl+C para fechar", border_style="red")

    with Live(build_view(), console=console, refresh_per_second=1) as live:
        try:
            while True:
                time.sleep(interval)
                live.update(build_view())
        except KeyboardInterrupt:
            pass


def run_gui(server_url, default_password=""):
    import gi
    gi.require_version("Gtk", "4.0")
    gi.require_version("Adw", "1")
    from gi.repository import Adw, GLib, Gtk

    class RadarWindow(Adw.ApplicationWindow):
        def __init__(self, app):
            super().__init__(application=app, title="Bloodborne - Radar de Mundos e Conexões")
            self.set_default_size(840, 560)
            self.tracker = SignTracker()

            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
            self.set_content(box)

            header = Adw.HeaderBar()
            refresh_btn = Gtk.Button(icon_name="view-refresh-symbolic")
            refresh_btn.connect("clicked", lambda _b: self.refresh_data())
            header.pack_start(refresh_btn)

            self.pwd_entry = Gtk.Entry(placeholder_text="Senha de rede (ex: cakey)")
            if default_password:
                self.pwd_entry.set_text(default_password)
            self.pwd_entry.connect("activate", lambda _e: self.refresh_data())
            header.pack_end(self.pwd_entry)
            box.append(header)

            self.status_label = Gtk.Label(label=f"Servidor: {server_url}", margin_top=6, margin_bottom=6)
            box.append(self.status_label)

            scroller = Gtk.ScrolledWindow(vexpand=True)
            self.list_box = Gtk.ListBox()
            self.list_box.set_selection_mode(Gtk.SelectionMode.NONE)
            self.list_box.add_css_class("boxed-list")
            scroller.set_child(self.list_box)
            box.append(scroller)

            # Events box at bottom
            events_label = Gtk.Label(label="<b>Feed de Eventos e Entradas:</b>", use_markup=True,
                                     halign=Gtk.Align.START, margin_start=12, margin_top=8)
            box.append(events_label)
            ev_scroller = Gtk.ScrolledWindow(vexpand=False, min_content_height=120)
            self.ev_box = Gtk.ListBox()
            self.ev_box.set_selection_mode(Gtk.SelectionMode.NONE)
            ev_scroller.set_child(self.ev_box)
            box.append(ev_scroller)

            self.refresh_data()
            GLib.timeout_add_seconds(3, self.auto_refresh)

        def auto_refresh(self):
            self.refresh_data()
            return True

        def refresh_data(self):
            pwd = self.pwd_entry.get_text().strip() or None
            ok, signs, err = fetch_summon_signs(server_url, password=pwd)
            events = self.tracker.update(signs if ok else [])

            while child := self.list_box.get_first_child():
                self.list_box.remove(child)
            while child := self.ev_box.get_first_child():
                self.ev_box.remove(child)

            if not ok:
                self.status_label.set_markup(f"<span color='red'>Erro: {err}</span>")
                return

            info_pwd = f" | Senha: <b>\"{pwd}\"</b>" if pwd else " | Todas públicas"
            self.status_label.set_markup(f"<b>{len(signs)}</b> caçador(es) com Kin Bell aguardando invocação{info_pwd}")

            if not signs:
                self.list_box.append(Adw.ActionRow(title="Nenhum caçador com Kin Bell ativo no momento."))
            else:
                for s in signs:
                    p = s["password"]
                    p_text = f"<span color='green'><b>Senha: \"{p}\"</b></span>" if p else "<span color='#e05252'>[Sem Senha]</span>"
                    row = Adw.ActionRow(title=s["name"], subtitle=f"Nv: {s['level']} | {s['area_name']} | {p_text}")
                    row.add_prefix(Gtk.Image.new_from_icon_name("avatar-default-symbolic"))
                    self.list_box.append(row)

            for t_str, text, color in reversed(events[-8:]):
                r = Adw.ActionRow(title=f"[{t_str}] {text}")
                self.ev_box.append(r)

    class RadarApp(Adw.Application):
        def __init__(self):
            super().__init__(application_id="com.bloodborne.radar")

        def do_activate(self):
            win = RadarWindow(self)
            win.present()

    app = RadarApp()
    app.run(None)


def main():
    parser = argparse.ArgumentParser(description="Bloodborne Radar de Mundos e Conexões")
    parser.add_argument("--server", default=DEFAULT_SERVER, help="URL WebAPI do servidor")
    parser.add_argument("--password", "-p", default="", help="Filtrar por senha (ex: cakey)")
    parser.add_argument("--gui", action="store_true", help="Abrir em janela gráfica")
    parser.add_argument("--interval", type=int, default=3, help="Intervalo em segundos")
    args = parser.parse_args()

    if args.gui:
        run_gui(args.server, default_password=args.password)
    else:
        run_terminal(args.server, password=args.password or None, interval=args.interval)


if __name__ == "__main__":
    main()
