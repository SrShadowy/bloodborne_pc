# SPDX-License-Identifier: GPL-2.0-or-later
"""The Hunter's Dream / hosts account flow behind the launcher's UI.

The public server signs players in the way TVs do it: a device code approved on
the website with Discord (/auth/device/start, then /auth/device/poll until it is
approved), or a local account whose one-time recovery code is the only way back
(/auth/account/create, /auth/recover). Every call answers with the ResKind
envelope: a refusal is ResKind != 0 with the reason in Message whatever the
HTTP status — a rate limit is a 429 carrying it.

This module owns the protocol and the {OnlineId, Token} file, with no GTK and no
game state, so the tests drive it against a local server (tests/
test_hosts_account.py). Per architecture guidelines the launcher owns the flow:
this is the client it calls, and the token reaches the game as BB_HOST_TOKEN /
BB_HOST_ONLINE_ID (bbport_config.game_environment) — the C++ side never
re-implements these endpoints.
"""
import json
import socket
import time
import urllib.error
import urllib.request

DEFAULT_SERVER = "thehuntersdream.com:18671"
TIMEOUT = 8  # seconds, as both reference clients call the account service
RULESET = "vanilla"  # no gameplay plugins, sent by both reference clients
DEVICE_NAME = "bbport on " + socket.gethostname()[:38]  # shown on the website (48 max)


class HostsError(Exception):
    """A call that failed: transport, HTTP, or a reply the protocol forbids."""


BbhostError = HostsError  # alias for backwards compatibility


def server_base(server=""):
    """The origin of the account service: "<scheme>://<host[:port]>".

    An empty setting is the public server; a bare "host[:port]" means https;
    only http and https are schemes — anything else is refused, not guessed.
    """
    value = (server or "").strip() or DEFAULT_SERVER
    scheme, sep, rest = value.partition("://")
    if sep:
        if scheme not in ("http", "https"):
            raise HostsError(f"unsupported scheme {scheme!r}: use http:// or https://")
        host = rest
    else:
        scheme, host = "https", scheme
    if ":" not in host:
        host += ":18671"
    return f"{scheme}://{host}"


def _call(base, path, body):
    """One POST and the ResKind envelope: whatever the status, the body speaks
    (a limit is 429 with the reason in Message, a refusal 2xx with ResKind != 0),
    exactly as both reference clients read it."""
    request = urllib.request.Request(
        base + path, data=json.dumps(body).encode(), method="POST",
        headers={"Accept": "application/json", "Content-Type": "application/json",
                 "X-BBHost-Ruleset": RULESET})
    try:
        with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
            status, text = response.status, response.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as error:  # the body still carries the envelope
        status, text = error.code, error.read().decode("utf-8", "replace")
    except OSError as error:  # refused, unreachable, timed out
        raise HostsError(f"network: {error}") from error
    try:
        reply = json.loads(text) if text.strip() else {}
    except ValueError as error:
        raise HostsError(f"HTTP {status}: the reply is not JSON") from error
    if not isinstance(reply, dict):
        raise HostsError(f"HTTP {status}: the reply is not a JSON object")
    if "ResKind" not in reply:
        raise HostsError(f"no account service here (HTTP {status})")
    if reply["ResKind"] != 0:
        raise HostsError(str(reply.get("Message")
                              or f"the server refused (ResKind {reply['ResKind']})"))
    return reply


def device_start(server=""):
    """Start a device link: the code the player approves on the website, plus the
    poll rhythm (seconds) and how long the code lives."""
    reply = _call(server_base(server), "/auth/device/start", {"DeviceName": DEVICE_NAME})
    link = {"DeviceCode": reply.get("DeviceCode"), "UserCode": reply.get("UserCode"),
            "VerificationUri": reply.get("VerificationUri"),
            "Interval": reply.get("Interval", 5), "ExpiresIn": reply.get("ExpiresIn", 600)}
    for field in ("DeviceCode", "UserCode", "VerificationUri"):
        if not isinstance(link[field], str) or not link[field]:
            raise HostsError(f"the reply has no {field}")
    if not isinstance(link["Interval"], int) or link["Interval"] < 1:
        link["Interval"] = 5
    if not isinstance(link["ExpiresIn"], int) or link["ExpiresIn"] < 1:
        link["ExpiresIn"] = 600
    return link


def device_poll(server, device_code):
    """One poll: the status is pending, slow_down, approved, denied or expired;
    approved is the only one that carries the sign-in."""
    reply = _call(server_base(server), "/auth/device/poll", {"DeviceCode": device_code})
    status = reply.get("Status")
    if status not in ("pending", "slow_down", "approved", "denied", "expired"):
        raise HostsError(f"unknown poll status {status!r}")
    if status == "approved":
        return status, *_sign_in(reply)
    return status, "", ""


def device_wait(server, link, cancel=None, sleep=None):
    """Poll the link until it is approved, denied or dies out: the sign-in
    (online_id, token) with an "approved" status, or the flow's end (denied,
    expired, cancelled). Waiting counts poll intervals the way the reference
    client does — slow_down slows it down; `cancel` (a threading.Event) stops it
    early with "cancelled", and `sleep` is where the waiting happens (the tests
    pass their own)."""
    sleep = sleep or (lambda seconds, cancel: cancel.wait(seconds) if cancel else time.sleep(seconds))
    interval, waited = link["Interval"], 0
    while waited < link["ExpiresIn"]:
        if cancel is not None and cancel.is_set():
            return "cancelled", "", ""
        sleep(interval, cancel)
        if cancel is not None and cancel.is_set():
            return "cancelled", "", ""
        status, online_id, token = device_poll(server, link["DeviceCode"])
        if status == "slow_down":
            interval += 1
        elif status != "pending":
            return status, online_id, token
        waited += interval
    return "expired", "", ""


def account_create(server, name):
    """An account held by this PC alone: the recovery code is the only way back."""
    return _signed(_call(server_base(server), "/auth/account/create",
                         {"Name": name, "DeviceName": DEVICE_NAME}))


def account_recover(server, name, code):
    """A new token and a new recovery code for the name that answers this code."""
    return _signed(_call(server_base(server), "/auth/recover",
                         {"Name": name, "RecoveryCode": code}))


def account_check(server, token):
    """Does the server still accept this PC's token? The lightest call that
    carries it (web_code, as the reference client signs in with): the envelope
    answers — ResKind 0 means the token passes, anything else is the server's
    own reason (expired, replaced) with no change to the account. The issued
    code itself is not kept: it exists for the website, not here."""
    if not token:
        raise HostsError("no token: link an account first")
    _call(server_base(server), "/auth/web_code", {"Token": token})
    return True


def _sign_in(reply):
    """A signed-in reply: the token and the account's name, both there or the
    reply is broken."""
    for field in ("Token", "OnlineId"):
        if not isinstance(reply.get(field), str) or not reply[field]:
            raise HostsError(f"the reply has no {field}")
    return reply["OnlineId"], reply["Token"]


def _signed(reply):
    """The sign-in with this call's recovery code: token, name and code, all three."""
    online_id, token = _sign_in(reply)
    code = reply.get("RecoveryCode")
    if not isinstance(code, str) or not code:
        raise HostsError("the reply has no RecoveryCode")
    return online_id, token, code


def load_account(path):
    """The saved sign-in {OnlineId, Token}; both empty when nobody signed in or
    the file says nothing usable (a broken file is nobody home, not a crash)."""
    try:
        data = json.loads(path.read_text())
    except (OSError, ValueError):
        return "", ""
    if not isinstance(data, dict):
        return "", ""
    online_id, token = data.get("OnlineId"), data.get("Token")
    if not isinstance(online_id, str) or not isinstance(token, str) or not token:
        return "", ""
    return online_id, token


def save_account(path, online_id, token):
    """Keep the sign-in where the launcher settings live, readable by its owner
    only (it holds the account token, like settings.json holds the password)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"OnlineId": online_id, "Token": token}, indent=2) + "\n")
    path.chmod(0o600)
