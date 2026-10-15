# SPDX-License-Identifier: GPL-2.0-or-later
"""netlab wire/protobuf helpers: encoding parity with bbport_shadnet, and the
round trip of every proto reply the boot reads."""
import socket
import struct
import sys
import unittest
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools" / "netlab"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "launcher"))

import netlab_wire as nw
import bbport_shadnet as bn


def _proto_body(kv):
    """A minimal encoder for server replies in test data (proto3 varint/str)."""
    out = bytearray()

    def field(n, v):
        if isinstance(v, bool):
            tag = (n << 3) | 0
            b = 1 if v else 0
        elif isinstance(v, str):
            enc = v.encode()
            btag = (n << 3) | 2
            out.append(btag)
            _varint(len(enc), out)
            out.extend(enc)
            return
        else:
            tag = (n << 3) | 0
            b = v
        out.append(tag)
        _varint(b, out)

    for n, v in kv.items():
        if isinstance(v, list):
            for item in v:
                pass
        elif isinstance(v, dict):
            # nested: build recursively outside
            pass
        else:
            field(n, v)
    return out


def _varint(v, out):
    while True:
        b = v & 0x7F
        v >>= 7
        out.append(b | 0x80 if v else b)
        if not v:
            return


class WireFramingParityTest(unittest.TestCase):
    def test_frame_byte_identical_to_launcher(self):
        a = nw.frame(nw.REQUEST, nw.CMD_LOGIN, 0, b"abc")
        b = bn.frame(bn.REQUEST, bn.LOGIN, 0, b"abc")
        self.assertEqual(a, b)
        self.assertAlmostEqual(len(a), bn.HEADER + 3)

    def test_login_payload_varint_tags_match(self):
        a = nw.login_payload("SrShadowy", "hunter2")
        b = bn.login_payload("SrShadowy", "hunter2")
        self.assertEqual(a, b)
        size = struct.unpack("<I", a[:4])[0]
        self.assertEqual(size, len(a) - 4)

    def test_split_server_ok(self):
        self.assertEqual(nw.split_server("srv.shadps4.net:31313"),
                        ("srv.shadps4.net", 31313))
        self.assertEqual(nw.split_server("srv.shadps4.net"), ("srv.shadps4.net", 31313))
        with self.assertRaises(nw.ShadnetError):
            nw.split_server("host:notaport")


class ProtoCodecRoundTripTest(unittest.TestCase):
    def test_parse_proto_reads_varint_and_length_delimited(self):
        body = bytearray()
        # field 2 varint = 42  -> tag 16 varint(42)
        body.append((2 << 3) | 0); _varint(42, body)
        # field 1 string "hi" -> tag 10, varint2, bytes
        body.append((1 << 3) | 2); _varint(2, body); body.extend(b"hi")
        p = nw.parse_proto(bytes(body))
        self.assertEqual(nw.field_int(p, 2), 42)
        self.assertEqual(nw.field_str(p, 1), "hi")

    def test_login_reply_decode(self):
        # build LoginReply by hand: avatar=1 str, user_id=2 varint, friends=3 msg
        msg_body = bytearray()
        # friend entry: npid="f1"(1 str), online=1(2 varint)
        fe = bytearray()
        fe.append((1 << 3) | 2); _varint(2, fe); fe.extend(b"f1")
        fe.append((2 << 3) | 0); _varint(1, fe)
        outer = bytearray()
        tag = (1 << 3) | 2
        _varint(tag, outer); _varint(3, outer); outer.extend(b"ava")
        outer.append((2 << 3) | 0); _varint(9, outer)
        outer.append((3 << 3) | 2); _varint(len(fe), outer); outer.extend(fe)
        proto = nw.parse_proto(bytes(outer))
        parsed = nw.parse_login_reply(proto)
        self.assertEqual(parsed["avatar_url"], "ava")
        self.assertEqual(parsed["user_id"], 9)
        self.assertEqual(parsed["friends"], [("f1", True)])

    def test_context_reply_decode_is_zero_proto(self):
        # ContextStart reply: error byte only -> split_reply returns (0, None)
        err, proto = nw.split_reply(bytes([0]))
        self.assertEqual(err, 0)
        self.assertIsNone(proto)

    def test_getworlds_decode(self):
        w = bytearray()
        w.append((1 << 3) | 0); _varint(7, w)   # world_id
        w.append((5 << 3) | 0); _varint(2, w)    # rooms_num
        w.append((6 << 3) | 0); _varint(4, w)    # room_members_num
        outer = bytearray()
        outer.append((1 << 3) | 2); _varint(len(w), outer); outer.extend(w)
        proto = nw.parse_proto(bytes(outer))
        worlds = nw.parse_worlds_reply(proto)
        self.assertEqual(worlds[0]["world_id"], 7)
        self.assertEqual(worlds[0]["rooms_num"], 2)
        self.assertEqual(worlds[0]["room_members_num"], 4)

    def test_search_reply_decode(self):
        # one MatchingRoomDataExternal: owner_npid=18, room_id=7, cur_members=2
        rm = bytearray()
        rm.append((2 << 3) | 0); _varint(2, rm)        # cur_members
        rm.append((7 << 3) | 0); _varint(999, rm)      # room_id
        _varint((18 << 3) | 2, rm); _varint(2, rm); rm.extend(b"hp")  # owner_npid
        outer = bytearray()
        outer.append((1 << 3) | 2); _varint(len(rm), outer); outer.extend(rm)
        outer.append((2 << 3) | 0); _varint(1, outer)  # range_start
        outer.append((3 << 3) | 0); _varint(1, outer)  # range_total
        outer.append((4 << 3) | 0); _varint(1, outer)  # range_result
        proto = nw.parse_proto(bytes(outer))
        parsed = nw.parse_search_reply(proto)
        self.assertEqual(parsed["range_total"], 1)
        self.assertEqual(len(parsed["rooms"]), 1)
        self.assertEqual(parsed["rooms"][0]["room_id"], 999)
        self.assertEqual(parsed["rooms"][0]["cur_members"], 2)
        self.assertEqual(parsed["rooms"][0]["owner_npid"], "hp")

    def test_room_members_decode(self):
        m = bytearray()
        m.append((1 << 3) | 2); _varint(1, m); m.extend(b"X")
        m.append((2 << 3) | 0); _varint(7, m)
        outer = bytearray()
        outer.append((1 << 3) | 2); _varint(len(m), outer); outer.extend(m)
        proto = nw.parse_proto(bytes(outer))
        mems = nw.parse_room_members_reply(proto)
        self.assertEqual(mems[0]["npid"], "X")
        self.assertEqual(mems[0]["member_id"], 7)

    def test_split_reply_ok_and_none(self):
        err, proto = nw.split_reply(bytes([3]) + struct.pack("<I", 0))
        # err=3, u32 size 0 => proto={}
        self.assertEqual(err, 3)
        self.assertEqual(proto, {})
        # body with u32 size
        body = b"\x00" + struct.pack("<I", 0) + b"extra"
        err, proto = nw.split_reply(body)
        self.assertEqual(err, 0)
        self.assertEqual(proto, {})


class BuilderTest(unittest.TestCase):
    def test_context_payload(self):
        payload = nw.context_payload(1)
        # u32 size 2, then field1 varint 1 (tag 8, value 1)  -> actually body = tag(1,0)+varint(1) => 2 bytes
        size = struct.unpack("<I", payload[:4])[0]
        self.assertEqual(size, 2)
        self.assertEqual(payload[4:], bytes([8, 1]))

    def test_search_payload_fields(self):
        payload = nw.search_payload(1, 0, 0, 0x7FFFFFFF)
        body = payload[4:]
        proto = nw.parse_proto(body)
        self.assertEqual(nw.field_int(proto, 1), 1)
        # range_max huge
        self.assertEqual(nw.field_int(proto, 4), 0x7FFFFFFF)


if __name__ == "__main__":
    unittest.main()
