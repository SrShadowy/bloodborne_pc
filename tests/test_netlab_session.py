# SPDX-License-Identifier: GPL-2.0-or-later
"""netlab shadNet session boot through a fake server, end to end."""
import socket
import struct
import sys
import threading
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "netlab"))

import netlab_boot
import netlab_session
import netlab_wire as nw

HEADER = 15

def _venc(v, out):
    while True:
        b = v & 0x7F
        v >>= 7
        out.append(b | 0x80 if v else b)
        if not v:
            return

def _kv(blob, pairs):
    blob = bytearray(blob)
    for n, val in pairs:
        if val is None:
            continue
        if isinstance(val, str):
            enc = val.encode()
            blob.append((n << 3) | 2)
            _venc(len(enc), blob)
            blob.extend(enc)
        elif isinstance(val, bool):
            blob.append((n << 3) | 0)
            _venc(1 if val else 0, blob)
        else:
            if val:
                blob.append((n << 3) | 0)
                _venc(val, blob)
            # bool/zero omitted per proto3... but numeric 0 default; fine
    return bytes(blob)

def _reply(cmd, packet_id, payload_bytes):
    # payload_bytes = bytes to place AFTER the 15-byte header (already includes err+u32+proto)
    return struct.pack("<BHIQ", nw.REPLY, cmd, HEADER + len(payload_bytes), packet_id) + payload_bytes

def _login_reply(err=0, user_id=42, npid="f1", online=True,
                 avatar="ava"):
    proto = bytearray()
    # avatar_url=1
    a = avatar.encode()
    proto.append((1 << 3) | 2); _venc(len(a), proto); proto.extend(a)
    # user_id=2
    proto.append((2 << 3) | 0); _venc(user_id, proto)
    # friends=3 (one FriendEntry: npid=f1 online=True)
    fe = bytearray()
    enc = npid.encode()
    fe.append((1 << 3) | 2); _venc(len(enc), fe); fe.extend(enc)
    fe.append((2 << 3) | 0); _venc(1 if online else 0, fe)
    proto.append((3 << 3) | 2); _venc(len(fe), proto); proto.extend(fe)
    body = struct.pack("<I", len(proto)) + proto if err == 0 else b""
    return bytes([err]) + (struct.pack("<I", len(proto)) + proto if err == 0 else b"")

def _token_reply(err=0):
    proto = bytearray()
    t = b"T" * 32
    proto.append((1 << 3) | 2); _venc(len(t), proto); proto.extend(t)
    proto.append((2 << 3) | 0); _venc(99, proto)
    enc = b"SrShadowy".decode().encode()
    proto.append((3 << 3) | 2); _venc(len(enc), proto); proto.extend(enc)
    return bytes([err]) + (struct.pack("<I", len(proto)) + proto if err == 0 else b"")

def _features_reply(matching2=True, err=0):
    proto = bytes([(1 << 3) | 0, 1 if matching2 else 0])
    if err:
        return bytes([err])
    return bytes([0]) + struct.pack("<I", len(proto)) + proto

def _worlds_reply():
    # one world: world_id=1, rooms_num=2, room_members_num=3
    w = bytearray()
    for n, v in ((1, 1), (2, 0), (3, 0), (4, 0), (5, 2), (6, 3)):
        # only encode nonzero — rooms_num 2, world_id 1
        pass
    w = bytearray()  # careful: rebuild
    # world_id=1
    w.append((1 << 3) | 0); _venc(1, w)
    # rooms_num=5 -> 2
    w.append((5 << 3) | 0); _venc(2, w)
    # room_members_num=6 -> 3
    w.append((6 << 3) | 0); _venc(3, w)
    body = bytearray()
    body.append((1 << 3) | 2); _venc(len(w), body); body.extend(w)
    return bytes([0]) + struct.pack("<I", len(body)) + bytes(body)

def _search_reply():
    rm = bytearray()
    # cur_members=2 (field2)
    rm.append((2 << 3) | 0); _venc(2, rm)
    # room_id=7 (field7)
    rm.append((7 << 3) | 0); _venc(1001, rm)
    # owner_npid=18
    o = b"hunterA"
    _venc((18 << 3) | 2, rm); _venc(len(o), rm); rm.extend(o)
    outer = bytearray()
    outer.append((1 << 3) | 2); _venc(len(rm), outer); outer.extend(rm)
    # range_start=2, total=3, result=4
    outer.append((2 << 3) | 0); _venc(1, outer)
    outer.append((3 << 3) | 0); _venc(1, outer)
    outer.append((4 << 3) | 0); _venc(1, outer)
    return bytes([0]) + struct.pack("<I", len(outer)) + bytes(outer)

def _members_reply(npids=("hunterA", "hunterB")):
    outer = bytearray()
    for n in npids:
        m = bytearray()
        b = n.encode()
        m.append((1 << 3) | 2); _venc(len(b), m); m.extend(b)
        m.append((2 << 3) | 0); _venc(1, m)
        outer.append((1 << 3) | 2); _venc(len(m), outer); outer.extend(m)
    return bytes([0]) + struct.pack("<I", len(outer)) + bytes(outer)

class FakeMatchingServer:
    def __init__(self, login_err=0, matching2_enabled=True, held=False):
        self.login_err = login_err
        self.matching2_enabled = matching2_enabled
        self.held = held
        self.requests = []
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(4)
        self.port = self.listener.getsockname()[1]
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    @property
    def server(self):
        return f"127.0.0.1:{self.port}"

    def _send(self, conn, data):
        try:
            conn.sendall(data)
        except OSError:
            pass

    def _serve(self):
        try:
            conn, _ = self.listener.accept()
        except OSError:
            return
        with conn:
            if self.held:
                time.sleep(self.held)
                return
            # ServerInfo push
            conn.sendall(struct.pack("<BHIQ", nw.SERVER_INFO, 0, 19, 0) +
                        struct.pack("<I", 1))
            while True:
                try:
                    ptype, cmd, pid, payload = nw.recv_frame(conn)
                except (OSError, nw.ShadnetError, EOFError, struct.error):
                    break
                self.requests.append(cmd)
                if cmd == nw.CMD_LOGIN:
                    self._send(conn, _reply(cmd, pid, _login_reply(self.login_err)))
                elif cmd == 13:
                    self._send(conn, _reply(cmd, pid, bytes([0])))
                elif cmd == nw.CMD_GET_TOKEN:
                    self._send(conn, _reply(cmd, pid, _token_reply()))
                elif cmd == nw.CMD_GET_FEATURES:
                    self._send(conn, _reply(cmd, pid, _features_reply(self.matching2_enabled)))
                elif cmd == nw.CMD_CONTEXT_START:
                    self._send(conn, _reply(cmd, pid, bytes([0])))
                elif cmd == nw.CMD_GET_WORLD_INFO:
                    self._send(conn, _reply(cmd, pid, _worlds_reply()))
                elif cmd == nw.CMD_SEARCH_ROOM:
                    self._send(conn, _reply(cmd, pid, _search_reply()))
                elif cmd == nw.CMD_GET_ROOM_MEMBERS:
                    self._send(conn, _reply(cmd, pid, _members_reply()))
                elif cmd == nw.CMD_CONTEXT_STOP:
                    self._send(conn, _reply(cmd, pid, bytes([0])))
                elif cmd == nw.CMD_TERMINATE:
                    self._send(conn, _reply(cmd, pid, bytes([0])))
                    break
                else:
                    # unknown/other command
                    self._send(conn, _reply(cmd, pid, bytes([0])))
                    break

    def close(self):
        try:
            self.listener.close()
        except OSError:
            pass

def _run_boot(server):
    sess = netlab_session.GameSession(server, "SrShadowy", "hunter2",
                                     timeout=4, search_max=20)
    return netlab_boot.run_boot(sess), sess

class FullBootTest(unittest.TestCase):
    def test_end_to_end_success(self):
        fake = FakeMatchingServer()
        try:
            result, sess = _run_boot(fake.server)
            self.assertTrue(result["ok"], result["steps"])
            names = [s["step"] for s in result["steps"]]
            self.assertIn("Login", names)
            self.assertIn("GetToken (WebAPI bearer)", names)
            self.assertIn("GetServerFeatures", names)
            self.assertIn("ContextStart", names)
            self.assertIn("GetWorldInfoList", names)
            self.assertTrue(any(n.startswith("SearchRoom") for n in names))
            self.assertTrue(any(n.startswith("Room members") for n in names))
            self.assertIn("ContextStop", names)
            self.assertIn("Terminate (clean disconnect)", names)
            # no commands mutated identity beyond login
            self.assertEqual(result["worlds"][0]["world_id"], 1)
            self.assertEqual(result["worlds"][0]["rooms_num"], 2)
            self.assertEqual(len(result["rooms"]), 1)
            self.assertTrue(result["rooms"][0]["owner_npid"].startswith("hunter"))
            # Terminate actually hit the wire
            self.assertIn(nw.CMD_TERMINATE, fake.requests)
        finally:
            fake.close()

    def test_room_member_npids_parsed(self):
        fake = FakeMatchingServer()
        try:
            result, sess = _run_boot(fake.server)
            # find a "Room members" step whose detail carries hunterA
            details = " ".join(s["detail"] for s in result["steps"])
            self.assertIn("hunterA", details)
        finally:
            fake.close()

    def test_login_refusal_aborts(self):
        fake = FakeMatchingServer(login_err=8)
        try:
            result, sess = _run_boot(fake.server)
            self.assertFalse(result["ok"])
            names = [s["step"] for s in result["steps"]]
            self.assertEqual(names[0], "ServerInfo handshake")
            self.assertEqual(names[1], "Login")
            self.assertNotIn("ContextStart", names)
            self.assertNotIn("GetWorldInfoList", names)
            self.assertNotIn(nw.CMD_CONTEXT_START, fake.requests)
            self.assertNotIn(nw.CMD_GET_WORLD_INFO, fake.requests)
        finally:
            fake.close()

    def test_matching_disabled_skips_matching_half(self):
        fake = FakeMatchingServer(matching2_enabled=False)
        try:
            result, sess = _run_boot(fake.server)
            names = [s["step"] for s in result["steps"]]
            self.assertTrue(any("Matching2 (disabled" in n for n in names))
            self.assertNotIn("ContextStart", names)
            self.assertNotIn("GetWorldInfoList", names)
            self.assertNotIn(nw.CMD_CONTEXT_START, fake.requests)
            self.assertNotIn(nw.CMD_GET_WORLD_INFO, fake.requests)
            # still terminated cleanly
            self.assertIn(nw.CMD_TERMINATE, fake.requests)
        finally:
            fake.close()

    def test_timings_present(self):
        fake = FakeMatchingServer()
        try:
            result, sess = _run_boot(fake.server)
            for s in result["steps"]:
                self.assertIn("ms", s)
                self.assertGreaterEqual(s["ms"], 0)
        finally:
            fake.close()
class LoginOnlyExtra(unittest.TestCase):
    def test_single_verdict(self):
        fake = FakeMatchingServer()
        try:
            sess = netlab_session.GameSession(fake.server, "SrShadowy", "hunter2", timeout=4)
            r = netlab_boot.run_login(sess)
            self.assertTrue(r.get("ok"))
        finally:
            fake.close()

class Silent (unittest.TestCase):
    def test_timeout(self):
        fake = FakeMatchingServer(held=10.0)
        try:
            sess = netlab_session.GameSession(fake.server, "SrShadowy", "hunter2", timeout=0.3)
            result = netlab_boot.run_boot(sess)
            self.assertFalse(result.get("ok", True))
            self.assertFalse(result["steps"][0]["ok"])
        finally:
            fake.close()
