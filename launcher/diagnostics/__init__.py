# SPDX-License-Identifier: GPL-2.0-or-later
"""Diagnostics and structured logging subsystem for BBPort."""
from .logging_setup import LOG, setup_logging, get_log_dir, get_log_file
from .system_info import (
    get_runtime_backend,
    detect_audio_server,
    get_distro_id,
    get_sdl3_install_command,
    collect_diagnostics_report,
)

__all__ = [
    "LOG",
    "setup_logging",
    "get_log_dir",
    "get_log_file",
    "get_runtime_backend",
    "detect_audio_server",
    "get_distro_id",
    "get_sdl3_install_command",
    "collect_diagnostics_report",
]
