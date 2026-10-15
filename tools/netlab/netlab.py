#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""netlab CLI: boot/login/map against Bloodborne's multiplayer stack."""
import argparse, json, os, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import netlab_boot, netlab_dream as dream, netlab_session, netlab_wire as wire

CONFIG_FILE = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home()/".config"))) \
    / "bbport-launcher" / "settings.json"


def _load_settings():
    out = {"server": "srv.shadps4.net:31313", "webapi": "", "community": "https://thehuntersdream.com",
           "npid": "", "password": ""}
    try:
        if CONFIG_FILE.is_file():
            d = json.loads(CONFIG_FILE.read_text(encoding="utf-8"))
            for a, b in (("online_server", "server"), ("online_webapi", "webapi"),
                         ("online_community", "community"), ("online_npid", "npid"),
                         ("online_password", "password")):
                if d.get(a):
                    out[b] = d[a]
    except (OSError, ValueError, TypeError):
        pass
    if os.environ.get("BB_SHADNET_SERVER"): out["server"] = os.environ["BB_SHADNET_SERVER"]
    if os.environ.get("BB_SHADNET_WEBAPI"): out["webapi"] = os.environ["BB_SHADNET_WEBAPI"]
    if os.environ.get("BB_COMMUNITY_SERVER"): out["community"] = os.environ["BB_COMMUNITY_SERVER"]
    if os.environ.get("BB_SHADNET_NPID"): out["npid"] = os.environ["BB_SHADNET_NPID"]
    if os.environ.get("BB_SHADNET_PASSWORD"): out["password"] = os.environ["BB_SHADNET_PASSWORD"]
    return out


def _resolve(args):
    s = _load_settings()
    if getattr(args, "server", None): s["server"] = args.server
    if getattr(args, "community", None): s["community"] = args.community
    if getattr(args, "webapi", None): s["webapi"] = args.webapi
    if getattr(args, "npid", None): s["npid"] = args.npid
    if getattr(args, "password", None): s["password"] = args.password
    if s["npid"] and not s["password"]:
        raise wire.ShadnetError("no password: set online_password in settings.json")
    return s


def _common(p):
    p.add_argument("--server"); p.add_argument("--community"); p.add_argument("--webapi")
    p.add_argument("--npid"); p.add_argument("--password"); p.add_argument("--base")
    p.add_argument("--timeout", type=float, default=8.0)
    p.add_argument("--search-max", type=int, dest="search_max", default=0)
    p.add_argument("--title-id", default=wire.TITLE_ID)
    p.add_argument("--json", action="store_true", dest="as_json")


def cmd_boot(args):
    s = _resolve(args)
    sess = netlab_session.GameSession(s["server"], s["npid"], s["password"],
                                     timeout=args.timeout, title_id=args.title_id,
                                     search_max=args.search_max)
    res = netlab_boot.run_boot(sess)
    if args.as_json:
        print(json.dumps(res, indent=2, default=str)); return
    print("netlab boot:", res["server"], bool(res["npid"]) and f"(NPID {res['npid']})" or "")
    for st in res["steps"]:
        mark = "OK " if st["ok"] else "FAIL"
        print(f"  [{mark}] {st['step']:34} {st['detail']}  ({st['ms']} ms)")
    passed = sum(1 for st in res["steps"] if st["ok"])
    print(f"result: {passed}/{len(res['steps'])} steps ok, {int(res['seconds']*1000)} ms total")


def cmd_login(args):
    s = _resolve(args)
    sess = netlab_session.GameSession(s["server"], s["npid"], s["password"],
                                     timeout=args.timeout, title_id=args.title_id)
    try:
        r = netlab_boot.run_login(sess)
    except Exception as e:
        print(f"login: {s['server']}: {e}", file=sys.stderr); return
    mark = "OK " if r.get("ok") else "FAIL"
    print(f"login: {s['server']} [{mark}] {r.get('text','')}")
    if r.get("ok"):
        print(f"  user_id={r.get('user_id')} friends={len(r.get('friends',[]))}")


def cmd_map(args):
    s = _resolve(args)
    base = args.base or s.get("community") or "https://thehuntersdream.com"
    if args.all_layers:
        args.live = args.messages = args.events = True
    if args.live or not (args.messages or args.events):
        d = dream.live(base, timeout=args.timeout, world=args.world)
        if args.as_json: print(json.dumps({"live": d}, indent=2, default=str))
        else:
            for line in dream.format_live(d, limit=args.limit): print(line)
    if args.messages:
        d = dream.messages(base, timeout=args.timeout)
        if not args.as_json:
            print("=== messages on the map ===")
            for line in dream.format_messages(d, limit=args.limit): print(line)
        elif not args.live:
            print(json.dumps({"messages": d}, indent=2, default=str))
    if args.events:
        d = dream.events(base, timeout=args.timeout)
        if not args.as_json:
            print("=== events ===")
            for line in dream.format_events(d, limit=args.limit): print(line)
    if args.as_json and (args.live and args.messages):
        pass


def main(argv=None):
    p = argparse.ArgumentParser(description="netlab")
    sub = p.add_subparsers(dest="command", required=True)
    b = sub.add_parser("boot"); _common(b); b.set_defaults(func=cmd_boot)
    l = sub.add_parser("login"); _common(l); l.set_defaults(func=cmd_login)
    m = sub.add_parser("map"); _common(m)
    m.add_argument("--limit", type=int, default=15)
    m.add_argument("--world", default=None)
    m.add_argument("--live", action="store_true", default=True)
    m.add_argument("--messages", action="store_true", default=True)
    m.add_argument("--events", action="store_true")
    m.add_argument("--all-layers", dest="all_layers", action="store_true")
    m.set_defaults(func=cmd_map)
    args = p.parse_args(argv)
    try:
        args.func(args)
    except Exception as e:
        print(f"netlab error: {e}", file=sys.stderr); return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
