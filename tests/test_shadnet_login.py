# SPDX-License-Identifier: GPL-2.0-or-later
"""The launcher's shadNet login check (bbport_shadnet) against a fake server.

The fake speaks the game's wire — pushed ServerInfo, a read Login request, a
scripted Reply — so every byte of the probe is asserted without any real shadNet
server. Cases: accepted, each refused verdict, frames in between, a silent
server, a dead port and a corrupt frame.
"""
import socket
import struct
import sys
import threading
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "launcher"))

import bbport_shadnet as shadnet  # noqa: E402

HEADER = 15


def build_frame(ptype, command, payload=b"", packet_id=0):
    """A frame built independently of the module (the server side of the wire)."""
    return struct.pack("<BHIQ", ptype, command, HEADER + len(payload), packet_id) + payload


def read_frame(sock):
    header = sock.recv(HEADER)
    if len(header) < HEADER:
        return None
    ptype, command, total, packet_id = struct.unpack("<BHIQ", header)
    payload = b""
    while len(payload) < total - HEADER:
        chunk = sock.recv(total - HEADER - len(payload))
        if not chunk:
            break
        payload += chunk
    return ptype, command, packet_id, payload


class FakeShadnet:
    """One scripted exchange per connection: optional ServerInfo and extra
    frames first, then the reply; the request frame is kept for asserts."""

    def __init__(self, replies, send_server_info=True, hold=0.0):
        self.replies = list(replies)
        self.send_server_info = send_server_info
        self.hold = hold  # keep the connection open after the replies (a silent server)
        self.request = None
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(4)
        self.port = self.listener.getsockname()[1]
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    @property
    def server(self):
        return f"127.0.0.1:{self.port}"

    def _serve(self):
        while True:
            try:
                conn, _addr = self.listener.accept()
            except OSError:
                return
            with conn:
                if self.send_server_info:
                    conn.sendall(build_frame(shadnet.SERVER_INFO, 0, b"v1"))
                request = read_frame(conn)
                if request is not None:
                    self.request = request
                for reply in self.replies:
                    try:
                        conn.sendall(reply)
                    except OSError:
                        break
                if self.hold:
                    time.sleep(self.hold)   # the client should give up first

    def close(self):
        self.listener.close()


class LoginPayloadTest(unittest.TestCase):
    def test_the_payload_is_the_games_login_request(self):
        payload = shadnet.login_payload("SrShadowy", "hunter2")
        size = struct.unpack("<I", payload[:4])[0]
        body = payload[4:]
        self.assertEqual(size, len(body))
        for text in (b"SrShadowy", b"hunter2", b"CUSA03173", b"Bloodborne"):
            self.assertIn(text, body)
        self.assertEqual(body[0], (1 << 3) | 2)  # field 1 (npid), wire type 2

    def test_the_frame_header_matches_the_game(self):
        packet = shadnet.frame(shadnet.REQUEST, shadnet.LOGIN, 0, b"abc")
        ptype, command, total, packet_id = struct.unpack("<BHIQ", packet[:HEADER])
        self.assertEqual((ptype, command, total, packet_id), (0, 0, HEADER + 3, 0))
        self.assertEqual(packet[HEADER:], b"abc")

    def test_the_server_setting_splits_the_way_the_game_does(self):
        self.assertEqual(shadnet.split_server("srv.shadps4.net:31313"), ("srv.shadps4.net", 31313))
        self.assertEqual(shadnet.split_server("srv.shadps4.net"), ("srv.shadps4.net", 31313))
        with self.assertRaises(shadnet.ShadnetError):
            shadnet.split_server("host:notaport")

    def test_empty_credentials_never_open_a_socket(self):
        with self.assertRaises(shadnet.ShadnetError):
            shadnet.check_login("127.0.0.1:1", "  ", "pw")


class CheckLoginTest(unittest.TestCase):
    def check(self, fake, npid="SrShadowy", password="hunter2"):
        try:
            return shadnet.check_login(fake.server, npid, password, timeout=3)
        finally:
            fake.close()

    def test_accepted_credentials_pass(self):
        fake = FakeShadnet([build_frame(shadnet.REPLY, shadnet.LOGIN, b"\x00" + b"\x00" * 4)])
        ok, text = self.check(fake)
        self.assertTrue(ok)
        self.assertEqual(text, "NoError (0)")

    def test_a_refused_password_says_which(self):
        fake = FakeShadnet([build_frame(shadnet.REPLY, shadnet.LOGIN, b"\x08")])
        ok, text = self.check(fake)
        self.assertFalse(ok)
        self.assertEqual(text, "LoginInvalidPassword (8)")

    def test_the_servers_generic_login_error_reads_as_refused(self):
        fake = FakeShadnet([build_frame(shadnet.REPLY, shadnet.LOGIN, b"\x05")])
        ok, text = self.check(fake)
        self.assertFalse(ok)
        self.assertEqual(text, "LoginError (5)")

    def test_already_logged_in_is_refused_but_explained(self):
        fake = FakeShadnet([build_frame(shadnet.REPLY, shadnet.LOGIN, b"\x06")])
        ok, text = self.check(fake)
        self.assertFalse(ok)
        self.assertEqual(text, "LoginAlreadyLoggedIn (6)")

    def test_frames_before_the_reply_are_skipped(self):
        fake = FakeShadnet([
            build_frame(shadnet.NOTIFICATION, 8, b"presence"),
            build_frame(shadnet.REPLY, 12, b"\x00"),   # another command's reply
            build_frame(shadnet.REPLY, shadnet.LOGIN, b"\x00"),
        ])
        ok, text = self.check(fake)
        self.assertTrue(ok)

    def test_the_request_the_server_received_is_the_games(self):
        fake = FakeShadnet([build_frame(shadnet.REPLY, shadnet.LOGIN, b"\x00")])
        self.check(fake)
        ptype, command, packet_id, payload = fake.request
        self.assertEqual((ptype, command, packet_id), (0, 0, 0))
        size = struct.unpack("<I", payload[:4])[0]
        self.assertEqual(payload[4 + size:], b"")  # u32-LE length wraps the proto exactly
        self.assertIn(b"SrShadowy", payload)
        self.assertIn(b"hunter2", payload)

    def test_a_silent_server_times_out_with_words(self):
        fake = FakeShadnet([], hold=1.0)   # reads the Login and never answers
        with self.assertRaisesRegex(shadnet.ShadnetError, "timed out"):
            shadnet.check_login(fake.server, "x", "y", timeout=0.4)
        fake.close()

    def test_a_dead_port_is_a_network_error(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
        listener.close()   # nothing listens there anymore
        with self.assertRaisesRegex(shadnet.ShadnetError, "network"):
            shadnet.check_login(f"127.0.0.1:{port}", "x", "y", timeout=2)

    def test_a_corrupt_frame_is_refused(self):
        fake = FakeShadnet([struct.pack("<BHIQ", shadnet.REPLY, shadnet.LOGIN, 3, 0)])
        with self.assertRaisesRegex(shadnet.ShadnetError, "corrupt"):
            shadnet.check_login(fake.server, "x", "y", timeout=2)
        fake.close()

    def test_a_closed_connection_reads_as_network(self):
        fake = FakeShadnet([b""])   # sendall of nothing: the connection just ends
        with self.assertRaises(shadnet.ShadnetError):
            shadnet.check_login(fake.server, "x", "y", timeout=2)
        fake.close()


if __name__ == "__main__":
    unittest.main()
