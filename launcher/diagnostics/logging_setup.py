# SPDX-License-Identifier: GPL-2.0-or-later
from __future__ import annotations

import faulthandler
import logging
import logging.handlers
import os
import platform
import sys
import tempfile
import threading
from pathlib import Path
from typing import Optional

APP_NAME = "bbport"
LOG = logging.getLogger(APP_NAME)
_CRASH_STREAM = None  # Keeps crash file open while faulthandler is active
_CONFIGURED = False
_LOG_DIR: Path | None = None


def get_log_dir() -> Path | None:
    """Returns the active log directory if configured."""
    return _LOG_DIR


def get_log_file() -> Path | None:
    """Returns the primary log file path if configured."""
    if _LOG_DIR:
        return _LOG_DIR / "launcher.log"
    return None


def _candidate_log_dirs() -> list[Path]:
    """Resolves potential log directories following Freedesktop XDG hierarchy."""
    candidates: list[Path] = []

    state_home = os.environ.get("XDG_STATE_HOME")
    if state_home and os.path.isabs(state_home):
        candidates.append(Path(state_home) / APP_NAME / "logs")
    else:
        candidates.append(Path.home() / ".local" / "state" / APP_NAME / "logs")

    cache_home = os.environ.get("XDG_CACHE_HOME")
    if cache_home and os.path.isabs(cache_home):
        candidates.append(Path(cache_home) / APP_NAME / "logs")
    else:
        candidates.append(Path.home() / ".cache" / APP_NAME / "logs")

    # Fallback to private user temp directory
    try:
        uid = os.getuid()
    except AttributeError:
        uid = 1000
    candidates.append(Path(tempfile.gettempdir()) / f"{APP_NAME}-{uid}" / "logs")
    return candidates


def _prepare_crash_log(log_dir: Path):
    """Limits crash log size between runs; keeps stream open for faulthandler."""
    path = log_dir / "fatal-crash.log"
    try:
        if path.exists() and path.stat().st_size > 1_000_000:
            old = log_dir / "fatal-crash.log.1"
            try:
                old.unlink(missing_ok=True)
            except OSError:
                pass
            path.replace(old)
        stream = path.open("a", encoding="utf-8", buffering=1)
        try:
            os.chmod(path, 0o600)
        except OSError:
            pass
        return stream
    except OSError:
        return None


def setup_logging() -> Path | None:
    """Configures centralized structured logging before GUI/SDL imports.

    Idempotent bootstrap: safe to call multiple times.
    """
    global _CRASH_STREAM, _CONFIGURED, _LOG_DIR

    if _CONFIGURED:
        return _LOG_DIR

    is_debug = os.environ.get("LAUNCHER_DEBUG") == "1" or os.environ.get("BBPORT_DEBUG") == "1"
    level = logging.DEBUG if is_debug else logging.INFO

    formatter = logging.Formatter(
        fmt="%(asctime)s.%(msecs)03d %(levelname)s [%(name)s] "
            "[pid=%(process)d thread=%(threadName)s] %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )

    log_dir: Optional[Path] = None
    for candidate in _candidate_log_dirs():
        try:
            candidate.mkdir(parents=True, exist_ok=True, mode=0o700)
            probe = candidate / ".write-test"
            probe.write_text("ok", encoding="utf-8")
            probe.unlink(missing_ok=True)
            log_dir = candidate
            break
        except OSError:
            continue

    handlers: list[logging.Handler] = []
    console = logging.StreamHandler(sys.stderr)
    console.setLevel(level)
    console.setFormatter(formatter)
    handlers.append(console)

    if log_dir is not None:
        try:
            file_handler = logging.handlers.RotatingFileHandler(
                log_dir / "launcher.log",
                maxBytes=2 * 1024 * 1024,
                backupCount=4,
                encoding="utf-8",
            )
            file_handler.setLevel(level)
            file_handler.setFormatter(formatter)
            handlers.append(file_handler)
        except OSError as exc:
            print(f"[!] Warning: Unable to open persistent log file: {exc}", file=sys.stderr)

        _CRASH_STREAM = _prepare_crash_log(log_dir)
        if _CRASH_STREAM is not None:
            try:
                faulthandler.enable(file=_CRASH_STREAM, all_threads=True)
            except (RuntimeError, OSError, ValueError):
                LOG.warning("Unable to enable faulthandler", exc_info=True)
    else:
        print("[!] Warning: No writable directory found for persistent logs.", file=sys.stderr)

    # Configure root logger to intercept third-party library logs
    root = logging.getLogger()
    for handler in root.handlers[:]:
        root.removeHandler(handler)
        try:
            handler.close()
        except Exception:
            pass
    root.setLevel(level)
    for handler in handlers:
        root.addHandler(handler)

    LOG.setLevel(level)
    LOG.propagate = True
    _CONFIGURED = True
    _LOG_DIR = log_dir

    def handle_uncaught(exc_type, exc_value, exc_tb):
        if issubclass(exc_type, KeyboardInterrupt):
            sys.__excepthook__(exc_type, exc_value, exc_tb)
            return
        LOG.critical(
            "Unhandled Python exception",
            exc_info=(exc_type, exc_value, exc_tb),
        )

    sys.excepthook = handle_uncaught

    if hasattr(threading, "excepthook"):
        def handle_thread_exception(args):
            LOG.error(
                "Unhandled exception in thread %s",
                args.thread.name if args.thread else "<unknown>",
                exc_info=(args.exc_type, args.exc_value, args.exc_traceback),
            )
        threading.excepthook = handle_thread_exception

    LOG.info("========== launcher.start ==========")
    LOG.info("python=%s executable=%s", sys.version.split()[0], sys.executable)
    LOG.info("platform=%s architecture=%s", platform.platform(), platform.machine())
    LOG.info("cwd=%s", os.getcwd())
    LOG.info("session_type=%r current_desktop=%r session_desktop=%r",
             os.environ.get("XDG_SESSION_TYPE"),
             os.environ.get("XDG_CURRENT_DESKTOP"),
             os.environ.get("XDG_SESSION_DESKTOP"))
    LOG.info("display_env WAYLAND_DISPLAY=%r DISPLAY_present=%s GDK_BACKEND=%r",
             os.environ.get("WAYLAND_DISPLAY"), bool(os.environ.get("DISPLAY")),
             os.environ.get("GDK_BACKEND"))
    LOG.info("persistent_log_dir=%s", str(log_dir) if log_dir else "unavailable")

    return log_dir
