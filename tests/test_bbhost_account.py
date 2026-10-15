# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests for the launcher's Hunter's Dream account client (bbport_bbhost).

The protocol module is driven against an in-process HTTP server on 127.0.0.1 —
no network, no GTK, no game — so every request the client sends and every reply
shape the server can answer (including the broken ones) is exercised. The
launcher settings export (BB_ONLINE_BACKEND / BB_HOST_*) is covered too, with the
online module stubbed on.
"""
import json
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT / "launcher") not in sys.path:
    sys.path.insert(0, str(ROOT / "launcher"))

import bbport_bbhost as bbhost  # noqa: E402
import bbport_config  # noqa: E402


class _Handler(BaseHTTPRequestHandler):
    """Answers each POST with the next scripted (status, reply) and records it."""

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        self.server.requests.append({
            "path": self.path,
            "body": json.loads(self.rfile.read(length)),
            "ruleset": self.headers.get("X-BBHost-Ruleset"),
            "accept": self.headers.get("Accept"),
            "token": self.headers.get("X-BB-Token"),
        })
        status, reply = (self.server.replies.pop(0) if self.server.replies
                         else (500, {"ResKind": 1, "Message": "no scripted reply left"}))
        payload = json.dumps(reply).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *args):  # the tests assert on the traffic, not on stderr
        pass


class ServerCase(unittest.TestCase):
    def setUp(self):
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), _Handler)
        self.server.replies = []
        self.server.requests = []
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.base = f"http://127.0.0.1:{self.server.server_address[1]}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()


class TestServerBase(unittest.TestCase):
    def test_empty_is_the_public_server(self):
        self.assertEqual(bbhost.server_base(""), "https://thehuntersdream.com:18671")
        self.assertEqual(bbhost.server_base(None), "https://thehuntersdream.com:18671")

    def test_bare_host_gains_https_and_the_port(self):
        self.assertEqual(bbhost.server_base("example.com"), "https://example.com:18671")
        self.assertEqual(bbhost.server_base(" example.com:9000 "), "https://example.com:9000")

    def test_scheme_is_taken_from_the_setting(self):
        self.assertEqual(bbhost.server_base("http://192.168.0.2"), "http://192.168.0.2:18671")
        self.assertEqual(bbhost.server_base("https://example.com:9000"),
                         "https://example.com:9000")

    def test_anything_but_http_is_refused(self):
        with self.assertRaises(bbhost.BbhostError):
            bbhost.server_base("gopher://example.com")


class TestDeviceFlow(ServerCase):
    def test_start_sends_the_wire_the_reference_clients_send(self):
        self.server.replies = [(200, {"ResKind": 0, "DeviceCode": "dc", "UserCode": "ABCD-1234",
                                      "VerificationUri": "https://example.com/link",
                                      "Interval": 5, "ExpiresIn": 600})]
        link = bbhost.device_start(self.base)
        self.assertEqual(link["UserCode"], "ABCD-1234")
        request = self.server.requests[0]
        self.assertEqual(request["path"], "/auth/device/start")
        self.assertTrue(request["body"]["DeviceName"].startswith("bbport on "))
        self.assertEqual(request["ruleset"], "vanilla")
        self.assertEqual(request["accept"], "application/json")
        self.assertIsNone(request["token"])  # auth calls carry no token yet

    def test_start_clamps_a_broken_rhythm(self):
        self.server.replies = [(200, {"ResKind": 0, "DeviceCode": "dc", "UserCode": "U",
                                      "VerificationUri": "https://example.com/link",
                                      "Interval": 0, "ExpiresIn": -3})]
        link = bbhost.device_start(self.base)
        self.assertEqual((link["Interval"], link["ExpiresIn"]), (5, 600))

    def test_start_without_a_code_is_broken(self):
        self.server.replies = [(200, {"ResKind": 0, "DeviceCode": "dc",
                                      "VerificationUri": "https://example.com/link"})]
        with self.assertRaisesRegex(bbhost.BbhostError, "UserCode"):
            bbhost.device_start(self.base)

    def test_poll_reads_every_status(self):
        for status, extra in (("pending", {}), ("slow_down", {}), ("denied", {}),
                              ("expired", {}),
                              ("approved", {"OnlineId": "Hunter", "Token": "tok"})):
            with self.subTest(status=status):
                self.server.replies = [(200, {"ResKind": 0, "Status": status, **extra})]
                result = bbhost.device_poll(self.base, "dc")
                if status == "approved":
                    self.assertEqual(result, ("approved", "Hunter", "tok"))
                else:
                    self.assertEqual(result, (status, "", ""))

    def test_poll_with_an_unknown_status_is_broken(self):
        self.server.replies = [(200, {"ResKind": 0, "Status": "confused"})]
        with self.assertRaisesRegex(bbhost.BbhostError, "confused"):
            bbhost.device_poll(self.base, "dc")

    def test_approved_without_a_token_is_broken(self):
        self.server.replies = [(200, {"ResKind": 0, "Status": "approved", "OnlineId": "Hunter"})]
        with self.assertRaisesRegex(bbhost.BbhostError, "Token"):
            bbhost.device_poll(self.base, "dc")

    def test_wait_polls_until_approved_and_keeps_the_rhythm(self):
        self.server.replies = [(200, {"ResKind": 0, "Status": "pending"}),
                               (200, {"ResKind": 0, "Status": "pending"}),
                               (200, {"ResKind": 0, "Status": "approved",
                                      "OnlineId": "Hunter", "Token": "tok"})]
        slept = []
        link = {"DeviceCode": "dc", "Interval": 5, "ExpiresIn": 600}
        result = bbhost.device_wait(self.base, link, sleep=lambda s, _c: slept.append(s))
        self.assertEqual(result, ("approved", "Hunter", "tok"))
        self.assertEqual(slept, [5, 5, 5])

    def test_wait_slows_down_when_the_server_says_so(self):
        self.server.replies = [(200, {"ResKind": 0, "Status": "slow_down"}),
                               (200, {"ResKind": 0, "Status": "approved",
                                      "OnlineId": "Hunter", "Token": "tok"})]
        slept = []
        link = {"DeviceCode": "dc", "Interval": 5, "ExpiresIn": 600}
        bbhost.device_wait(self.base, link, sleep=lambda s, _c: slept.append(s))
        self.assertEqual(slept, [5, 6])

    def test_wait_stops_on_cancel_before_asking_the_server(self):
        cancel = threading.Event()
        cancel.set()
        link = {"DeviceCode": "dc", "Interval": 5, "ExpiresIn": 600}
        result = bbhost.device_wait(self.base, link, cancel=cancel,
                                    sleep=lambda s, _c: self.fail("slept while cancelled"))
        self.assertEqual(result, ("cancelled", "", ""))
        self.assertEqual(self.server.requests, [])

    def test_wait_ends_when_the_code_dies_out(self):
        self.server.replies = [(200, {"ResKind": 0, "Status": "pending"}),
                               (200, {"ResKind": 0, "Status": "pending"})]
        slept = []
        link = {"DeviceCode": "dc", "Interval": 5, "ExpiresIn": 10}
        result = bbhost.device_wait(self.base, link, sleep=lambda s, _c: slept.append(s))
        self.assertEqual(result, ("expired", "", ""))
        self.assertEqual(slept, [5, 5])
        self.assertEqual(len(self.server.requests), 2)


class TestAccountCalls(ServerCase):
    def test_create_sends_name_and_device_and_reads_the_code(self):
        self.server.replies = [(200, {"ResKind": 0, "OnlineId": "Hunter", "Token": "tok",
                                      "RecoveryCode": "RC-1"})]
        self.assertEqual(bbhost.account_create(self.base, "Hunter"), ("Hunter", "tok", "RC-1"))
        request = self.server.requests[0]
        self.assertEqual(request["path"], "/auth/account/create")
        self.assertEqual(request["body"]["Name"], "Hunter")

    def test_recover_reads_the_new_code(self):
        self.server.replies = [(200, {"ResKind": 0, "OnlineId": "Hunter", "Token": "tok2",
                                      "RecoveryCode": "RC-2"})]
        self.assertEqual(bbhost.account_recover(self.base, "Hunter", "RC-1"),
                         ("Hunter", "tok2", "RC-2"))
        self.assertEqual(self.server.requests[0]["body"], {"Name": "Hunter",
                                                           "RecoveryCode": "RC-1"})

    def test_a_reply_without_the_recovery_code_is_broken(self):
        self.server.replies = [(200, {"ResKind": 0, "OnlineId": "Hunter", "Token": "tok"})]
        with self.assertRaisesRegex(bbhost.BbhostError, "RecoveryCode"):
            bbhost.account_create(self.base, "Hunter")


class TestFailures(ServerCase):
    def test_a_refusal_is_the_message_whatever_the_status(self):
        self.server.replies = [(429, {"ResKind": 2, "Message": "the daily link limit"})]
        with self.assertRaisesRegex(bbhost.BbhostError, "daily link limit"):
            bbhost.device_start(self.base)

    def test_a_refusal_without_a_message_says_it_refused(self):
        self.server.replies = [(200, {"ResKind": 1})]
        with self.assertRaisesRegex(bbhost.BbhostError, "refused"):
            bbhost.device_start(self.base)

    def test_a_reply_without_reskind_is_not_the_account_service(self):
        self.server.replies = [(404, {"error": "not found"})]
        with self.assertRaisesRegex(bbhost.BbhostError, "no account service"):
            bbhost.device_start(self.base)

    def test_a_body_that_is_not_json_is_reported(self):
        self.server.replies = [(200, None)]  # the handler json-dumps None -> "null"
        with self.assertRaisesRegex(bbhost.BbhostError, "not a JSON object"):
            bbhost.device_start(self.base)

    def test_an_unreachable_server_is_a_network_failure(self):
        with self.assertRaisesRegex(bbhost.BbhostError, "^network:"):
            bbhost.device_start("http://127.0.0.1:1")


class TestAccountFile(unittest.TestCase):
    def test_save_and_load_roundtrip(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "account.json"
            bbhost.save_account(path, "Hunter", "tok")
            self.assertEqual(bbhost.load_account(path), ("Hunter", "tok"))
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)

    def test_nobody_home_is_empty(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "account.json"
            self.assertEqual(bbhost.load_account(path), ("", ""))
            path.write_text("[1, 2]")
            self.assertEqual(bbhost.load_account(path), ("", ""))
            path.write_text('{"OnlineId": "Hunter"}')  # no token: not signed in
            self.assertEqual(bbhost.load_account(path), ("", ""))


class TestGameEnvironmentExport(unittest.TestCase):
    """The launcher settings reach the game as env vars (plan §3, §9)."""

    def run_env(self, account=None, **extra):
        settings = dict(bbport_config.DEFAULTS, online=True, extra_env="")
        settings.update(extra)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "account.json"
            if account:
                bbhost.save_account(path, *account)
            with mock.patch.object(bbport_config, "online_module", lambda: True), \
                    mock.patch.object(bbport_config, "ACCOUNT_FILE", path):
                return bbport_config.game_environment(settings)

    def test_shadnet_stays_the_compatible_default(self):
        env = self.run_env()
        self.assertEqual(env["BB_ONLINE_BACKEND"], "shadnet")
        self.assertNotIn("BB_HOST_TOKEN", env)
        self.assertEqual(env["BB_SHADNET_SERVER"], bbport_config.online.SHADNET_SERVER)

    def test_an_unknown_backend_is_shadnet(self):
        self.assertEqual(self.run_env(online_backend="foo")["BB_ONLINE_BACKEND"], "shadnet")

    def test_bbhost_exports_the_account_service_and_the_saved_token(self):
        env = self.run_env(account=("Hunter", "tok"), online_backend="bbhost")
        self.assertEqual(env["BB_ONLINE_BACKEND"], "bbhost")
        self.assertEqual(env["BB_HOST_SCHEME"], "https")
        self.assertEqual(env["BB_HOST_SERVER"], "thehuntersdream.com:18671")
        self.assertEqual(env["BB_HOST_ONLINE_ID"], "Hunter")
        self.assertEqual(env["BB_HOST_TOKEN"], "tok")
        # the shadNet coop path keeps working: its switch lands with the session backend
        self.assertEqual(env["BB_SHADNET_SERVER"], bbport_config.online.SHADNET_SERVER)

    def test_a_own_server_is_taken_as_it_is(self):
        env = self.run_env(online_backend="bbhost", online_bbhost_server="http://192.168.0.2:9000")
        self.assertEqual(env["BB_HOST_SCHEME"], "http")
        self.assertEqual(env["BB_HOST_SERVER"], "192.168.0.2:9000")

    def test_a_bad_custom_server_falls_back_to_the_public_one(self):
        env = self.run_env(online_backend="bbhost", online_bbhost_server="gopher://x")
        self.assertEqual(env["BB_HOST_SCHEME"], "https")
        self.assertEqual(env["BB_HOST_SERVER"], "thehuntersdream.com:18671")


if __name__ == "__main__":
    unittest.main()
