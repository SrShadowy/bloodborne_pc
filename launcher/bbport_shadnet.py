# SPDX-License-Identifier: GPL-2.0-or-later
"""A shadNet sign-in check from the launcher: the game's own wire, one Login.

The game's client (gpu/bbnet/shadps4/shadnet) speaks a framed protocol: a
15-byte header (type, command, total size, packet id; client.h SHAD_HEADER_SIZE),
a Login request whose payload is a u32-LE length and the LoginRequest protobuf
fields (shadnet.proto: npid=1, password=2, token=3, title_id=4, title_name=5,
appear_offline=6), and a Reply whose payload starts with an ErrorType byte
(NoError is the only pass). This module replays exactly that — one connection,
one Login, the verdict, close — so "is this NPID and password right" can be
answered from the launcher without starting the game, where the check would
otherwise only appear once in the log as a rejected login.

Standard library only (socket, struct), no GTK; the tests drive it against a
local fake server (tests/test_shadnet_login.py). Nothing contacts a real shadNet
server unless the player presses the check button.
"""
import socket
import struct

HEADER = 15  # SHAD_HEADER_SIZE: type u8, command u16 LE, total u32 LE, packet id u64 LE
REQUEST, REPLY, NOTIFICATION, SERVER_INFO = 0, 1, 2, 3
LOGIN = 0  # CommandType::Login
DEFAULT_PORT = 31313  # what the game's np_handler falls back to
MAX_PAYLOAD = 0x800000  # SHAD_MAX_PACKET_SIZE
TIMEOUT = 8  # seconds, as the account calls

# ErrorType (client.h): the reply's first byte decides the check.
ERRORS = {
    0: "NoError", 1: "Malformed", 2: "Invalid", 3: "InvalidInput", 4: "TooSoon",
    5: "LoginError", 6: "LoginAlreadyLoggedIn", 7: "LoginInvalidUsername",
    8: "LoginInvalidPassword", 9: "LoginInvalidToken",
}

# The game's LoginRequest carries the serial and title read from its own
# param.sfo (ElfInfo); the server logs them and authenticates on npid/password.
TITLE_ID = "CUSA03173"
TITLE_NAME = "Bloodborne"


class ShadnetError(Exception):
    """No verdict reached: transport, timeout, or a reply the protocol forbids."""


def split_server(server):
    """(host, port) from a "host[:port]" setting, the game's own fallback."""
    host, _sep, port = (server or "").strip().rpartition(":")
    if not host:
        return (server or "").strip(), DEFAULT_PORT
    try:
        return host, int(port)
    except ValueError as error:
        raise ShadnetError(f"bad port in {server!r}") from error


def _varint(value):
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        out.append(byte | 0x80 if value else byte)
        if not value:
            return bytes(out)


def _field(number, text):
    """One protobuf string field: tag (wire type 2), length, bytes."""
    data = text.encode()
    return bytes([(number << 3) | 2]) + _varint(len(data)) + data


def login_payload(npid, password):
    """The game's LoginRequest: u32-LE length, then npid, password and title
    (the token field stays out — the launcher has no shadNet token setting)."""
    body = (_field(1, npid) + _field(2, password)
            + _field(4, TITLE_ID) + _field(5, TITLE_NAME))
    return struct.pack("<I", len(body)) + body


def frame(ptype, command, packet_id, payload=b""):
    """One packet: the 15-byte header followed by the payload."""
    return struct.pack("<BHIQ", ptype, command, HEADER + len(payload), packet_id) + payload


def _recv_exactly(sock, size):
    chunks = []
    while size:
        chunk = sock.recv(size)
        if not chunk:
            raise ShadnetError("the server closed the connection")
        chunks.append(chunk)
        size -= len(chunk)
    return b"".join(chunks)


def _recv_frame(sock):
    ptype, command, total, _packet_id = struct.unpack("<BHIQ", _recv_exactly(sock, HEADER))
    if total < HEADER or total > HEADER + MAX_PAYLOAD:
        raise ShadnetError(f"corrupt frame (total={total})")
    return ptype, command, _recv_exactly(sock, total - HEADER)


def check_login(server, npid, password, timeout=TIMEOUT):
    """(ok, text): one Login to `server`, the verdict of its Reply.

    The ErrorType byte is the verdict (only NoError passes); whatever comes
    first — the pushed ServerInfo, notifications — is skipped, up to 8 frames.
    Transport problems raise ShadnetError, whose text is for the player."""
    if not npid.strip() or not password:
        raise ShadnetError("no NPID or password")
    host, port = split_server(server)
    try:
        with socket.create_connection((host, port), timeout=timeout) as sock:
            sock.settimeout(timeout)
            sock.sendall(frame(REQUEST, LOGIN, 0, login_payload(npid, password)))
            for _ in range(8):
                ptype, command, payload = _recv_frame(sock)
                if ptype == REPLY and command == LOGIN:
                    if not payload:
                        raise ShadnetError("empty login reply")
                    code = payload[0]
                    return code == 0, f"{ERRORS.get(code, 'error')} ({code})"
            raise ShadnetError("no login reply")
    except socket.timeout as error:
        raise ShadnetError("timed out") from error
    except OSError as error:
        raise ShadnetError(f"network: {error}") from error
