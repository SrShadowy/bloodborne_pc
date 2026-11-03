#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""[extra] C-007/F2 — game-log drain that cannot stall the producer.

F0 (A/B proof, testes/c007_f0_backpressure_ab.ps1) showed the launcher's black screen was
log backpressure: bb-probe -> run.py -> launcher run through TWO pipes in series, the GTK
main loop published one line per read_line_async callback, telemetry bursts saturated it,
the ~4 KB Windows pipe filled (measured: 4041 bytes, frozen), and bb-probe blocked in
write() BEFORE its first present (black at 20 s). Sending stdout straight to a file made the
game render the title at 144 FPS (31 974 bytes in the same 20 s).

This module removes the stall structurally and is platform-neutral (the per-OS split lives
in bbport_proc_win/linux.py): a dedicated thread drains the pipe at full speed into a
BOUNDED queue (oldest lines are dropped and counted when full), and the GTK loop receives a
single batched insert every PUBLISH_MS milliseconds. Neither pipe can ever fill, and the
main loop does O(1) textview work per tick regardless of burst size.

Used by the live LauncherWindow (launcher/bbport_launcher.py) and kept in sync in the
GameRunnerMixin (launcher/bbport_game_runner.py).
"""

import collections
import threading

QUEUE_MAX_LINES = 4000   # bounded memory; overflow drops the OLDEST lines (counted + shown)
PUBLISH_MS = 250         # one TextView batch insert per tick
PUBLISH_BATCH = 600      # max lines published per tick (1500+ lines/s sustained)
READ_CHUNK = 65536       # bytes per blocking read on the pipe


def start_log_pump(owner):
    """Starts draining owner.stream (a GInputStream from Gio.Subprocess.get_stdout_pipe).

    owner must provide append_log(text) and may provide _should_log_line(line) -> bool
    (absent = show everything). All GTK calls happen on the main loop via a GLib timer.
    The pump ends itself on EOF; starting a new pump supersedes the previous one.
    """
    from gi.repository import GLib

    gen = getattr(owner, "_log_pump_gen", 0) + 1
    owner._log_pump_gen = gen
    state = {"lines": collections.deque(), "dropped": 0, "eof": threading.Event()}

    def reader():
        buf = b""
        lines, eof = state["lines"], state["eof"]
        try:
            while True:
                chunk = owner.stream.read_bytes(READ_CHUNK, None)  # blocks; empty at EOF
                data = chunk.get_data() if chunk is not None else b""
                if not data:
                    break
                buf += data
                parts = buf.split(b"\n")
                buf = parts.pop()
                for raw in parts:
                    line = raw.decode("utf-8", "replace").rstrip("\r")
                    if len(lines) >= QUEUE_MAX_LINES:
                        lines.popleft()
                        state["dropped"] += 1
                    lines.append(line)
            if buf:
                lines.append(buf.decode("utf-8", "replace"))
        except Exception as error:  # broken pipe on Stop, etc.
            lines.append(f"[launcher] log reader ended: {error!r}")
        finally:
            eof.set()

    def publish():
        if gen != owner._log_pump_gen:  # superseded by a newer Start
            return False
        lines = state["lines"]
        keep = getattr(owner, "_should_log_line", None)
        batch = []
        while lines and len(batch) < PUBLISH_BATCH:
            line = lines.popleft()
            if keep is None or keep(line):
                batch.append(line)
        if batch:
            owner.append_log("\n".join(batch) + "\n")
        if state["dropped"]:
            dropped, state["dropped"] = state["dropped"], 0
            owner.append_log(f"[launcher] … {dropped} log lines skipped (full log in logs/) …\n")
        return not (state["eof"].is_set() and not lines)

    threading.Thread(target=reader, daemon=True, name="bbport-log-drain").start()
    GLib.timeout_add(PUBLISH_MS, publish)
