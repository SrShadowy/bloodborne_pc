# SPDX-License-Identifier: GPL-2.0-or-later
"""shadNet wire protocol helper for netlab: framing, protobuf-lite and decoded
replies. A self-contained copy of the wire bbport_shadnet.py speaks (the
game's client church::shadnet), extended with protobuf decoding and the
matching commands the launcher does not use. Standard library only.

Source of truth:
  - gpu/bbnet/shadps4/shadnet/{client.h, client.cpp, shadnet.proto}
  - https://github.com/shadps4-emu/shadNet documentation/{protocol,matching}.md
"""
import socket
import struct
HEADER = 15  # SHAD_HEADER_SIZE: type u8, command u16 LE, total u32 LE, packet id u64 LE
MAX_PAYLOAD = 0x800000  # SHAD_MAX_PACKET_SIZE
REQUEST, REPLY, NOTIFICATION, SERVER_INFO = 0, 1, 2, 3
DEFAULT_PORT = 31313

# Command IDs (CommandType), only the subset this emulator needs.
CMD_LOGIN = 0; CMD_TERMINATE = 1; CMD_GET_FEATURES = 12; CMD_SET_CLIENT_VERSION = 13; CMD_GET_TOKEN = 39
CMD_CONTEXT_START = 100; CMD_SEARCH_ROOM = 104; CMD_CONTEXT_STOP = 106
CMD_GET_WORLD_INFO = 111; CMD_GET_ROOM_MEMBERS = 114
ERRORS = {
    0: "NoError", 1: "Malformed", 2: "Invalid", 3: "InvalidInput", 4: "TooSoon",
    5: "LoginError", 6: "LoginAlreadyLoggedIn", 7: "LoginInvalidUsername",
    8: "LoginInvalidPassword", 9: "LoginInvalidToken",
    10: "CreationError", 11: "CreationExistingUsername",
    12: "CreationBannedEmailProvider", 13: "CreationExistingEmail",
    14: "RoomMissing", 15: "RoomAlreadyJoined", 16: "RoomFull",
    17: "RoomPasswordMismatch", 18: "RoomPasswordMissing",
    19: "RoomGroupNoJoinLabel", 20: "RoomGroupFull",
    21: "RoomGroupJoinLabelNotFound", 22: "RoomGroupMaxSlotMismatch",
    23: "Unauthorized", 24: "DbFail", 25: "EmailFail", 26: "NotFound",
    27: "Blocked", 28: "AlreadyFriend", 29: "ScoreNotBest",
    30: "ScoreInvalid", 31: "ScoreHasData", 32: "CondFail", 33: "Unsupported",
}
TITLE_ID, TITLE_NAME = "CUSA03173", "Bloodborne"


class ShadnetError(Exception):
    """Wire transport or protocol failure, with a one-line message for the user."""


def split_server(server):
    """(host, port) from 'host[:port]', matching the game's fallback."""
    server = (server or "").strip()
    if ":" in server:
        host, port = server.rsplit(":", 1)
        try:
            return host, int(port)
        except ValueError:
            raise ShadnetError(f"bad port in {server!r}")
    return server, DEFAULT_PORT


def varint_encode(value):
    """Encode an unsigned integer as a protobuf varint."""
    out = bytearray()
    v = value
    while True:
        byte = v & 0x7F
        v >>= 7
        out.append(byte | 0x80 if v else byte)
        if not v:
            return bytes(out)


def _varint_at(data, pos):
    shift = 0
    out = 0
    while True:
        if pos >= len(data):
            raise ShadnetError("bad varint")
        b = data[pos] & 0x7F
        out |= b << shift
        pos += 1
        if not (data[pos - 1] & 0x80):
            break
        shift += 7
    return out, pos


def tag_varint(number, value, body):
    if value:
        body.extend(varint_encode((number << 3) | 0))
        body.extend(varint_encode(value))


def tag_str(number, text, body):
    if text:
        body.extend(varint_encode((number << 3) | 2))
        enc = text.encode("utf-8")
        body.extend(varint_encode(len(enc)))
        body.extend(enc)


def proto_blob(body):
    """Wrap a protobuf body as the wire's size-prefixed blob (proto blob)."""
    return struct.pack("<I", len(body)) + bytes(body)


def login_payload(npid, password, title_id=TITLE_ID, title_name=TITLE_NAME):
    """LoginRequest as a proto blob (u32-LE size + LoginRequest fields)."""
    body = bytearray()
    tag_str(1, npid, body)
    tag_str(2, password, body)
    tag_str(4, title_id, body)
    tag_str(5, title_name, body)
    return proto_blob(body)


def client_version_payload(version):
    body = bytearray()
    tag_str(1, version, body)
    return proto_blob(body)


def context_payload(ctx_id):
    body = bytearray()
    tag_varint(1, ctx_id, body)
    return proto_blob(body)


def worlds_payload(server_id=1):
    body = bytearray()
    tag_varint(1, server_id, body)
    return proto_blob(body)


def search_payload(world_id=1, lobby_id=0, range_start=0, range_max=0):
    body = bytearray()
    tag_varint(1, world_id, body)
    tag_varint(2, lobby_id, body)
    tag_varint(3, range_start, body)
    tag_varint(4, range_max, body)
    return proto_blob(body)


def room_members_payload(room_id):
    body = bytearray()
    tag_varint(1, room_id, body)
    return proto_blob(body)


def parse_proto(blob):
    """Decode a proto blob body (the bytes after the u32-LE size) into a dict:
    field number -> list of ('v', int) / ('b', bytes) tagged entries."""
    out = {}
    pos = 0
    while pos < len(blob):
        tag, pos = _varint_at(blob, pos)
        number, wire = tag >> 3, tag & 7
        if wire == 0:
            v, pos = _varint_at(blob, pos)
            out.setdefault(number, []).append(("v", v))
        elif wire == 2:
            length, pos = _varint_at(blob, pos)
            value = blob[pos:pos + length]
            pos += length
            out.setdefault(number, []).append(("b", value))
        elif wire == 1:
            (v,) = struct.unpack("<Q", blob[pos:pos + 8]); pos += 8
            out.setdefault(number, []).append(("v", v))
        elif wire == 5:
            (v,) = struct.unpack("<I", blob[pos:pos + 4]); pos += 4
            out.setdefault(number, []).append(("v", v))
        else:
            raise ShadnetError(f"proto wire type {wire} unsupported")
    return out


def field_bytes(proto, number):
    """First length-delimited raw bytes for a field, or None."""
    for kind, val in proto.get(number, []):
        if kind == "b":
            return val
    return None


def field_str(proto, number, default=""):
    raw = field_bytes(proto, number)
    if raw is None:
        return default
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return default


def field_int(proto, number, default=0):
    for kind, val in proto.get(number, []):
        if kind == "v":
            return val
    return default


def sub_messages(proto, number):
    """All repeated message fields under `number`, each decoded."""
    return [parse_proto(v) for (kind, v) in proto.get(number, []) if kind == "b"]


def split_reply(frame_payload):
    """Reply payload = ErrorType byte, then (on success) u32-LE size + proto body.
    Returns (error_code, proto_dict_or_None)."""
    if not frame_payload:
        raise ShadnetError("empty reply")
    error = frame_payload[0]
    body = frame_payload[1:]
    if len(body) >= 4:
        size = struct.unpack("<I", body[:4])[0]
        if size and 4 + size <= len(body):
            blob = body[4:4 + size]
            return error, parse_proto(blob)
        if size == 0:
            return error, {}
    return error, None


def frame(ptype, command, packet_id=0, payload=b""):
    return struct.pack("<BHIQ", ptype, command, HEADER + len(payload), packet_id) + payload


def recv_exactly(sock, size):
    buf = b""
    while len(buf) < size:
        chunk = sock.recv(size - len(buf))
        if not chunk:
            raise ShadnetError("the server closed the connection")
        buf += chunk
    return buf


def recv_frame(sock):
    header = recv_exactly(sock, HEADER)
    ptype, command, total, packet_id = struct.unpack("<BHIQ", header)
    if total < HEADER or total > HEADER + MAX_PAYLOAD:
        raise ShadnetError(f"corrupt frame (total={total})")
    payload = recv_exactly(sock, total - HEADER)
    return ptype, command, packet_id, payload


# --- reply proto decoders ---------------------------------------------------

def _str_repeat(proto, number):
    out = []
    for (kind, val) in proto.get(number, []):
        if kind == "b":
            try:
                out.append(val.decode("utf-8"))
            except UnicodeDecodeError:
                pass
    return out


def parse_login_reply(proto):
    return {
        "avatar_url": field_str(proto, 1),
        "user_id": field_int(proto, 2),
        "friends": [(field_str(m, 1), bool(field_int(m, 2)))
                    for m in sub_messages(proto, 3)],
        "requests_sent": _str_repeat(proto, 4),
        "requests_received": _str_repeat(proto, 5),
        "blocked": _str_repeat(proto, 6),
    }


def parse_token_reply(proto):
    return {"token": field_str(proto, 1), "user_id": field_int(proto, 2),
            "npid": field_str(proto, 3)}


def parse_features_reply(proto):
    return {"matching2_enabled": bool(field_int(proto, 1))}


def parse_worlds_reply(proto):
    return [{"world_id": field_int(w, 1), "lobbies_num": field_int(w, 2),
            "max_lobby_members": field_int(w, 3), "lobby_members_num": field_int(w, 4),
            "rooms_num": field_int(w, 5), "room_members_num": field_int(w, 6)}
           for w in sub_messages(proto, 1)]


def parse_search_reply(proto):
    rooms = [{"max_slot": field_int(r, 1), "cur_members": field_int(r, 2),
             "flags": field_int(r, 3), "server_id": field_int(r, 4),
             "world_id": field_int(r, 5), "lobby_id": field_int(r, 6),
             "room_id": field_int(r, 7), "owner_npid": field_str(r, 18),
             "owner_account_id": field_int(r, 19), "owner_platform": field_int(r, 20),
             "external_int_attrs": [(field_int(m, 2), field_int(m, 3))
                                    for m in sub_messages(r, 15)]}
            for r in sub_messages(proto, 1)]
    return {"rooms": rooms, "range_start": field_int(proto, 2),
            "range_total": field_int(proto, 3), "range_result": field_int(proto, 4)}


def parse_room_members_reply(proto):
    return [{"npid": field_str(m, 1), "member_id": field_int(m, 2),
             "account_id": field_int(m, 3), "platform": field_int(m, 4),
             "join_date": field_int(m, 5), "role": field_int(m, 6)}
           for m in sub_messages(proto, 1)]
