# SPDX-License-Identifier: GPL-2.0-or-later
"""The Hunter's Dream site API: hunters abroad and messages, exactly what its
map page shows (worldmap.js reads /api/map/live and /api/map/messages)."""
import json
import urllib.parse
import urllib.request

USER_AGENT = "netlab/1.0 (shadNet connection tester)"


def _get(url, timeout=10.0):
    req = urllib.request.Request(url,
                                headers={"User-Agent": USER_AGENT,
                                         "Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as reply:
        return json.loads(reply.read(16_000_000).decode("utf-8", errors="replace"))


def site_base(community_url):
    """A community server URL -> the host that also serves the stats site."""
    u = (community_url or "https://thehuntersdream.com").strip()
    u = u.lstrip("/")
    if not u.startswith("http"):
        u = "https://" + u
    parsed = urllib.parse.urlsplit(u)
    scheme = parsed.scheme or "https"
    host = parsed.hostname or parsed.netloc.split(":")[0]
    return f"{scheme}://{host}"


def live(base, timeout=10.0, world=None):
    url = site_base(base).rstrip("/") + "/api/map/live"
    if world:
        url += "?" + urllib.parse.urlencode({"world": world})
    return _get(url, timeout)


def messages(base, timeout=10.0):
    return _get(site_base(base).rstrip("/") + "/api/map/messages", timeout)


def events(base, timeout=10.0):
    return _get(site_base(base).rstrip("/") + "/api/map/events", timeout)


def area_groups(d):
    """Return [(area_name, hunter_count)] from a live payload."""
    out = []
    for item in d.get("areas") or []:
        if isinstance(item, (list, tuple)) and len(item) >= 2:
            out.append((str(item[0]), int(item[1]) if str(item[1]).isdigit() else item[1]))
    return out


def format_live(d, limit=None):
    lines = []
    hunters = d.get("hunters") or []
    areas = area_groups(d)
    total = sum(c for _, c in areas) if areas else len(hunters)
    lines.append(f"hunters abroad now: {total}")
    if d.get("coordinates") is False:
        lines.append("  (positions are counted by area; map pixels off)")
    for area, count in sorted(areas, key=lambda x: -x[1] if isinstance(x[1], int) else 0):
        lines.append(f"  {area}: {count}")
    shown = 0
    # group hunters by area for a readable listing
    by_area = {}
    for h in hunters:
        by_area.setdefault(str(h.get("area", "?")), []).append(h)
    for area in sorted(by_area):
        if shown >= (limit or 12):
            break
        group = by_area[area]
        lines.append(f"  --- {area} ({len(group)}) ---")
        for h in group[: max(1, (limit or 12) - shown)]:
            hp = h.get("hp")
            hp_s = f" HP {hp[0]}/{hp[1]}" if isinstance(hp, list) and len(hp) == 2 else ""
            bell = (", ringing the " + h.get("bell")) if isinstance(h.get("bell"), str) else ""
            lvl = f" Lv {h.get('level')}" if h.get("level") else ""
            name = str(h.get("name") or "a hunter")
            age = _ago(h.get("age_s"))
            extra = h.get("rules") or ""
            lines.append(f"    {name}{lvl}{hp_s}{bell} - {age}" +
                        (f"  ({extra})" if extra else ""))
            shown += 1
    if len(hunters) > shown:
        lines.append(f"  ... and {len(hunters) - shown} more hunters not shown")
    return lines


def format_messages(d, limit=15):
    msgs = d.get("messages")
    if not isinstance(msgs, list):
        msgs = []
    lines = [f"messages on the map: {len(msgs)}"]
    shown = 0
    for m in msgs:
        if shown >= (limit or len(msgs)):
            break
        if not (isinstance(m, (list, tuple)) and len(m) >= 4):
            continue
        px, py = m[0], m[1]
        text = str(m[3]).strip() or "(a message)"
        acc = "exact" if (len(m) > 2 and m[2] == 1) else "area-fit"
        by = str(m[9]) if len(m) > 9 and m[9] else "a hunter"
        area = str(m[7]) if len(m) > 7 and m[7] else "?"
        gesture = f", gesture {m[4]}" if len(m) > 4 and m[4] else ""
        fine = m[5] if (len(m) > 5 and isinstance(m[5], (int, float))) else 0
        poor = m[6] if (len(m) > 6 and isinstance(m[6], (int, float))) else 0
        rate = f"  Fine {fine} . Poor {poor}" if (fine or poor) else ""
        lines.append(f"  [{px},{py}] \"{text}\"  By {by}  ({area}, {acc}{gesture}){rate}")
        shown += 1
    if len(msgs) > shown:
        lines.append(f"  ... {len(msgs) - shown} more messages not shown")
    return lines


def format_events(d, limit=5):
    deaths = d.get("deaths") or []
    kills = d.get("kills") or []
    bosses = d.get("bosses") or []
    lines = [f"events: {len(deaths)} deaths, {len(kills)} kills, {len(bosses)} boss kills"]
    # deaths by killer
    from collections import Counter
    killer_counter = Counter()
    for dd in deaths:
        if isinstance(dd, (list, tuple)) and len(dd) >= 4:
            killer_counter[str(dd[3]) or "unknown"] += 1
    for killer, n in killer_counter.most_common(limit):
        lines.append(f"  {n} killed by {killer}")
    return lines


def _ago(sec):
    try:
        sec = float(sec)
    except (TypeError, ValueError):
        return ""
    if sec < 90:
        return f"{int(sec)} s ago"
    if sec < 5400:
        return f"{int(sec / 60)} min ago"
    if sec < 172800:
        return f"{int(sec / 3600)} h ago"
    return f"{int(sec / 86400)} days ago"