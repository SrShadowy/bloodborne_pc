#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""[extra] Platform dispatcher for the game-process specialists (C-007).

Play/Stop is implemented per OS — bbport_proc_win.py (Windows: taskkill /T against the
launcher -> run.py -> bb-probe.exe tree) and bbport_proc_linux.py (POSIX: setsid/killpg
process-group semantics). This module imports the matching one; bbport_game_runner.py only
ever calls these four names, so a platform fix is confined to its own file.
"""

import sys

if sys.platform == "win32":
    from bbport_proc_win import PLATFORM, kill_tree, spawn_argv, stop_tree
else:
    from bbport_proc_linux import PLATFORM, kill_tree, spawn_argv, stop_tree

__all__ = ["PLATFORM", "spawn_argv", "stop_tree", "kill_tree"]
