# SPDX-License-Identifier: GPL-2.0-or-later
"""The "game emulator" side of netlab: the shadNet boot sequence from the
game's entry as far as matchmaking goes, driven by netlab_wire. Everything
reads/writes TCP only; nothing mutates player identity or creates rooms."""
import netlab_wire as wire
import socket
import struct
import time


class GameSession:
    """Drive one connection through the shadNet wire like a launching game."""

    def __init__(self, server, npid, password, timeout=8,
                 title_id=wire.TITLE_ID, title_name=wire.TITLE_NAME,
                 search_max=0):
        self.server = server
        self.npid = npid
        self.password = password
        self.timeout = timeout
        self.title_id = title_id
        self.title_name = title_name
        if search_max <= 0:
            search_max = (1 << 31) - 1
        self.search_max = search_max
        self.sock = None
        self.version = None
        self.user_id = 0
        self.friends = []
        self.matching2_enabled = False
        self.worlds = []
        self.all_rooms = []
        self.ctx_id = 1
        self._pid = 0

    # basic framing -----------------------------------------------------------

    def _open(self):
        host, port = wire.split_server(self.server)
        self.sock = socket.create_connection((host, port), timeout=self.timeout)
        self.sock.settimeout(self.timeout)

    def close(self):
        sock = self.sock
        self.sock = None
        if sock:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                sock.close()
            except OSError:
                pass

    def _read_server_info(self):
        """The server pushes ServerInfo (type 3) on accept. Wait up to 4s."""
        deadline = time.monotonic() + 4.0
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            self.sock.settimeout(max(0.2, min(self.timeout, remaining)))
            try:
                ptype, command, packet_id, payload = wire.recv_frame(self.sock)
            except socket.timeout:
                break
            except wire.ShadnetError:
                raise    # transport issue; let the caller report it
            if ptype == wire.SERVER_INFO:
                if len(payload) >= 4:
                    self.version = struct.unpack("<I", payload[:4])[0]
                return True
            # Notifications or stray ServerInfo-negative frames: ignore while waiting.
        return self.version is not None

    # request/reply ----------------------------------------------------------

    def _exchange(self, command, payload):
        pid = self._pid
        self._pid += 1
        self.sock.sendall(wire.frame(wire.REQUEST, command, pid, payload))
        started = time.monotonic()
        frames = 0
        while True:
            if self.sock is None:
                raise wire.ShadnetError("connection closed")
            self.sock.settimeout(self.timeout)
            try:
                ptype, cmd, packet_id, payload = wire.recv_frame(self.sock)
            except socket.timeout:
                raise wire.ShadnetError("timed out waiting for reply")
            if ptype == wire.NOTIFICATION:
                continue
            if ptype == wire.SERVER_INFO:
                if len(payload) >= 4 and self.version is None:
                    self.version = struct.unpack("<I", payload[:4])[0]
                continue
            if ptype == wire.REPLY and cmd == command and packet_id == pid:
                return payload
            frames += 1
            if frames > 32:
                raise wire.ShadnetError("no matching reply; server silent")

    # --- individual commands -----------------------------------------------

    def connect(self):
        self._open()
        if not self._read_server_info():
            raise wire.ShadnetError("no ServerInfo handshake (version mismatch?)")
        return {"version": self.version}

    def login(self):
        if not self.npid.strip() or not self.password:
            raise wire.ShadnetError("no NPID or password")
        payload = wire.login_payload(self.npid, self.password, self.title_id,
                                    self.title_name)
        body = self._exchange(wire.CMD_LOGIN, payload)
        code, proto = wire.split_reply(body)
        if code == wire.ShadnetError and False:  # never
            pass
        name = wire.ERRORS.get(code, f"unknown({code})")
        if code != 0:
            return {"ok": False, "error": code, "text": f"{name} ({code})"}
        parsed = wire.parse_login_reply(proto or {})
        self.user_id = parsed["user_id"]
        self.friends = parsed["friends"]
        n_on = sum(1 for _n, on in parsed["friends"] if on)
        detail = f"{name} ({code}), user_id {parsed['user_id']}, " \
                 f"friends {len(parsed['friends'])} ({n_on} online)"
        if parsed["avatar_url"]:
            detail += f", avatar {parsed['avatar_url']}"
        return {"ok": True, "error": code, "text": detail, "user_id": parsed["user_id"],
                "avatar_url": parsed["avatar_url"], "friends": parsed["friends"],
                "requests_sent": parsed["requests_sent"],
                "requests_received": parsed["requests_received"],
                "blocked": parsed["blocked"]}

    def set_client_version(self, version="netlab (test connection)"):
        payload = wire.client_version_payload(version)
        body = self._exchange(wire.CMD_SET_CLIENT_VERSION, payload)
        code, proto = wire.split_reply(body)
        name = wire.ERRORS.get(code, f"unknown({code})")
        return {"ok": code == 0, "error": code,
                "text": f"{name} ({code}), reported {version!r}"}

    def get_token(self):
        body = self._exchange(wire.CMD_GET_TOKEN, b"")
        code, proto = wire.split_reply(body)
        name = wire.ERRORS.get(code, f"unknown({code})")
        if code != 0 or proto is None:
            self.matching2_enabled = False
            return {"ok": code == 0, "error": code,
                    "text": f"{name} ({code})" if code != 0 else
                    f"{name} ({code}), no WebAPI token"}
        parsed = wire.parse_token_reply(proto)
        self.bearer_len = len(parsed["token"])
        # token itself is discarded: never stored or logged beyond its length
        return {"ok": True, "error": code,
                "text": f"{name} ({code}), WebAPI bearer {self.bearer_len} chars, "
                        f"user_id {parsed['user_id']}",
                "user_id": parsed["user_id"], "npid": parsed["npid"],
                "token_len": self.bearer_len}

    def get_features(self):
        body = self._exchange(wire.CMD_GET_FEATURES, b"")
        code, proto = wire.split_reply(body)
        name = wire.ERRORS.get(code, f"unknown({code})")
        enabled = False
        if code == 0 and proto is not None:
            enabled = wire.parse_features_reply(proto).get("matching2_enabled", False)
        self.matching2_enabled = bool(enabled) if code == 0 else False
        text = f"{name} ({code}), matching2 {'enabled' if enabled else 'disabled'}"
        if code != 0:
            text = f"{name} ({code}), assuming matching2 disabled"
        return {"ok": code == 0, "error": code, "text": text,
                "matching2_enabled": self.matching2_enabled, "version": self.version}

    def context_start(self):
        body = self._exchange(wire.CMD_CONTEXT_START, wire.context_payload(self.ctx_id))
        code, proto = wire.split_reply(body)
        name = wire.ERRORS.get(code, f"unknown({code})")
        return {"ok": code == 0, "error": code, "text": f"{name} ({code})",
                "ctx_id": self.ctx_id}

    def world_list(self, server_id=1):
        body = self._exchange(wire.CMD_GET_WORLD_INFO, wire.worlds_payload(server_id))
        code, proto = wire.split_reply(body)
        name = wire.ERRORS.get(code, f"unknown({code})")
        worlds = wire.parse_worlds_reply(proto) if (code == 0 and proto is not None) else []
        if worlds:
            bits = "; ".join(
                f"world {w['world_id']}: rooms {w['rooms_num']} ({w['room_members_num']} members)"
                for w in worlds)
            text = f"{name} ({code}), {bits}"
        else:
            text = f"{name} ({code}), no worlds reported"
        self.worlds = worlds
        return {"ok": code == 0, "error": code, "text": text,
                "server_id": server_id, "worlds": worlds}

    def search_rooms(self, world_id, lobby_id=0, range_start=1):
        payload = wire.search_payload(world_id, lobby_id, range_start, self.search_max)
        body = self._exchange(wire.CMD_SEARCH_ROOM, payload)
        code, proto = wire.split_reply(body)
        name = wire.ERRORS.get(code, f"unknown({code})")
        parsed = wire.parse_search_reply(proto) if (code == 0 and proto is not None) else {
            "rooms": [], "range_start": 0, "range_total": 0, "range_result": 0}
        rooms = parsed["rooms"]
        # filters: candidate rooms (world/lobby match comes from server already).
        total_all = []
        out = list(rooms)
        result_seq = "1"
        rs = parsed["range_result"]
        rt = parsed["range_total"]
        # automatic paging when more rooms than the page limit (20 per page)
        cur = range_start
        while code == 0 and parsed.get("range_result", 0) > 0 and \
                (parsed.get("range_start", 0) + parsed.get("range_result", 0) - 1
                 < parsed.get("range_total", 0)):
            cur += parsed["range_result"]
            body2 = self._exchange(wire.CMD_SEARCH_ROOM,
                                   wire.search_payload(world_id, lobby_id, cur,
                                       self.search_max))
            c2, p2 = wire.split_reply(body2)
            if c2 != 0:
                code = c2
                break
            p2r = wire.parse_search_reply(p2) if p2 is not None else None
            if not p2r:
                break
            out.extend(p2r["rooms"])
            parsed["range_total"] = p2r["range_total"]
        if code != 0:
            name = wire.ERRORS.get(code, f"unknown({code})")
        text = f"{name} ({code}), {len(out)} rooms of {parsed.get('range_total', rt)}"
        self.all_rooms.extend(out)
        return {"ok": code == 0, "error": code, "text": text,
                "world_id": world_id, "lobby_id": lobby_id,
                "range_start": range_start, "range_total": parsed.get("range_total", rt),
                "rooms": out}

    def room_members(self, room_id):
        body = self._exchange(wire.CMD_GET_ROOM_MEMBERS,
                              wire.room_members_payload(room_id))
        code, proto = wire.split_reply(body)
        name = wire.ERRORS.get(code, f"unknown({code})")
        if code == 0 and code == wire.ERRORS and False:
            pass
        members = wire.parse_room_members_reply(proto) if (code == 0 and proto is not None) else []
        npids = ", ".join(m["npid"] for m in members) or "(none)"
        text = f"{name} ({code}), members: {npids}" if code == 0 else f"{name} ({code})"
        return {"ok": code == 0, "error": code, "text": text,
                "room_id": room_id, "members": members}

    def context_stop(self):
        body = self._exchange(wire.CMD_CONTEXT_STOP, wire.context_payload(self.ctx_id))
        code, proto = wire.split_reply(body)
        # ContextStop reply carries error byte only
        name = wire.ERRORS.get(code, f"unknown({code})")
        return {"ok": code == 0, "error": code, "text": f"{name} ({code})"}

    def terminate(self):
        if self.sock is None:
            return {"ok": True, "error": 0, "text": "no connection"}
        try:
            # Terminate (1) has no body at all per protocol reference.
            self.sock.sendall(wire.frame(wire.REQUEST, wire.CMD_TERMINATE, self._pid, b""))
            self._pid += 1
            try:
                ptype, cmd, packet_id, payload = wire.recv_frame(self.sock)
            except (socket.timeout, wire.ShadnetError):
                return {"ok": True, "error": 0,
                        "text": "NoError (0) (clean close)"}
            if ptype == wire.REPLY and len(payload) >= 1:
                code = payload[0]
                name = wire.ERRORS.get(code, f"unknown({code})")
                return {"ok": code == 0, "error": code, "text": f"{name} ({code})"}
            return {"ok": True, "error": 0, "text": "NoError (0)"}
        except OSError:
            return {"ok": True, "error": 0, "text": "NoError (0)"}

