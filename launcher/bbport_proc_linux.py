#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""[extra] Linux/POSIX game-process specialist: spawn argv and tree termination.

Why this file exists (C-007): Play/Stop semantics differ fundamentally per OS, so each
platform lives in its own module (dispatcher: bbport_proc.py) — a fix on one side cannot
touch the other.

Linux facts this module relies on:
* start: `setsid bash run.sh` puts the game in its own session/process group whose id is
  the child's PID, and run.sh ends with `exec "$probe"` — bb-probe IS that process. There
  are no grandchildren to orphan: one signal to the group reaches everything.
* stop: os.killpg(pid, SIGTERM) hits the whole group; the 3 s fallback is SIGKILL.
Windows has no killpg/process groups — see bbport_proc_win.py.

Every branch here is POSIX-verbatim from the pre-split bbport_game_runner.py.
"""

import os
import signal
from pathlib import Path

PLATFORM = "linux"


def spawn_argv(port_dir) -> list:
    """Argv for Gio.SubprocessLauncher.spawnv (new session so killpg has a group)."""
    return ["setsid", "bash", str(Path(port_dir) / "run.sh")]


def stop_tree(pid, process):
    """Graceful stop: SIGTERM to the whole process group; force_exit only if signaling failed."""
    try:
        os.killpg(pid, signal.SIGTERM)
    except OSError:
        process.force_exit()


def kill_tree(pid, process):
    """3 s fallback: SIGKILL to the group — nothing survives this on POSIX."""
    try:
        os.killpg(pid, signal.SIGKILL)
    except OSError:
        pass
