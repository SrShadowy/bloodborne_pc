#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""[extra] Windows game-process specialist: spawn argv and tree termination (taskkill /T).

Why this file exists (C-007): Play/Stop semantics differ fundamentally per OS, so each
platform lives in its own module (dispatcher: bbport_proc.py) — a fix on one side cannot
touch the other.

Windows facts this module relies on:
* start: `python run.py` (console child), and run.py then spawns bb-probe.exe (the GUI
  process) — launcher -> run.py -> bb-probe.exe, a 3-process tree. There is no POSIX
  process group and no os.killpg on Windows.
* stop: Gio's force_exit()/TerminateProcess kills only the DIRECT child (run.py), which
  used to leave bb-probe.exe running as an orphan after Stop (user-reported bug, C-007).
  The fix is `taskkill /T`, which walks the process tree from run.py's PID:
    - stop_tree  (/T, no /F): posts WM_CLOSE; bb-probe.exe owns the game window and gets a
      graceful shutdown, so its shader-cache flush (FlushTick) can run. Console children
      may ignore WM_CLOSE — fine: only bb-probe.exe holds game state.
    - kill_tree  (/T /F, the 3 s fallback): guaranteed teardown of the whole tree, plus
      force_exit() so the Gio.Subprocess handle/accounting is reaped even if run.py is
      already gone.
"""

import subprocess
import sys
from pathlib import Path

PLATFORM = "win32"

# taskkill is a console tool; from the windowed launcher it must not flash a console.
_NO_WINDOW = 0x08000000  # CREATE_NO_WINDOW


def spawn_argv(port_dir) -> list:
    """Argv for Gio.SubprocessLauncher.spawnv: run.py prepares and starts the game."""
    return [sys.executable, str(Path(port_dir) / "run.py")]


def _taskkill(pid, force):
    cmd = ["taskkill", "/T", "/PID", str(pid)]
    if force:
        cmd.append("/F")
    subprocess.run(cmd, creationflags=_NO_WINDOW, check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def stop_tree(pid, process):
    """Graceful tree stop (no /F): bb-probe.exe gets WM_CLOSE and can flush its caches."""
    _taskkill(pid, force=False)


def kill_tree(pid, process):
    """3 s fallback: forced tree kill (/T /F) + reap the Gio.Subprocess handle."""
    _taskkill(pid, force=True)
    if process is not None:
        process.force_exit()
