# SPDX-License-Identifier: GPL-2.0-or-later
"""The Hunter's Dream site API client and its formatters."""
import http.server
import json
import socket
import threading
import time
import unittest
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "netlab"))

import netlab_dream as dream

LIVE = {"hunters": [{"name": "Dane", "area": "Central Yharnam",
                     "level": 42, "hp": [400, 450], "age_s": 10,
                     "bell": "Sinister", "rules": "inv"}],
        "areas": [["Central Yharnam", 3], [" Cathedral Ward", 1]],
        "coordinates": True, "minutes": 2}

MESSAGES = {"messages": [[10, 20, 1, "Hello, hunter", 0, 5, 1,
                          "Central Yharnam", 756, "HunterX"]]}


class FakeDream(http.server.ThreadingHTTPServer):
    allow_reuse_address = True

    def handle_one_request(self):
        super().handle_one_request()


class DreamHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self, *a, **k):
        path = self.path.split("?")[0]
        body = LIVE if path == "/api/map/live" else (MESSAGES if path == "/api/map/messages"
              else {"deaths": [["b", "k"]], "kills": [], "bosses": []})
        payload = json.dumps(body).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_request(self, *a, **k):
        pass


def fake_server():
    srv = FakeDream(("127.0.0.1", 0), DreamHandler)
    port = srv.server_port
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    time.sleep(0.2)
    return srv, port


class SiteBaseTest(unittest.TestCase):
    def test_normalises_community_address(self):
        b = dream.site_base("thehuntersdream.com:9000")
        self.assertEqual(b, "https://thehuntersdream.com")


class LiveFetchTest(unittest.TestCase):
    def test_parses_and_formats(self):
        with __import__("unittest.mock", fromlist=["patch"]).patch.object(dream, "_get", return_value=LIVE):
            d = dream.live("thehuntersdream.com")
        self.assertEqual(d["hunters"][0]["name"], "Dane")
        lines = dream.format_live(d, limit=10)
        text = "\n".join(lines)
        self.assertIn("hunters abroad now: 4", text)
        self.assertIn("Central Yharnam: 3", text)
        self.assertIn("Dane", text)
        self.assertIn("Lv 42", text)


class MessageFetchTest(unittest.TestCase):
    def test_tail_shape_and_formatter(self):
        with __import__("unittest.mock", fromlist=["patch"]).patch.object(dream, "_get", return_value=MESSAGES):
            d = dream.messages("thehuntersdream.com")
        self.assertEqual(d["messages"][0][3], "Hello, hunter")
        lines = dream.format_messages(d, limit=5)
        text = "\n".join(lines)
        self.assertIn("Hello, hunter", text)
        self.assertIn("HunterX", text)
        self.assertIn("Fine 5 . Poor 1", text)


class EventsTest(unittest.TestCase):
    def test_events(self):
        d = {"deaths": [[1, 2, 0, "k"]], "kills": [], "bosses": []}
        lines = dream.format_events(d, limit=5)
        self.assertIn("1 deaths", "\n".join(lines))
        self.assertIn("killed by k", "\n".join(lines))


if __name__ == "__main__":
    unittest.main()
