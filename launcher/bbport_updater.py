# SPDX-License-Identifier: GPL-2.0-or-later
"""Non-intrusive asynchronous update checker for BBPort Launcher.

Queries GitHub releases in a background daemon thread, notifies the user
via a subtle Libadwaita Toast if a new release is available, and provides
a direct link to the release page. Never downloads or installs without consent.
"""
from __future__ import annotations

import json
import os
import threading
import urllib.request
from typing import Callable, Optional

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, Gdk, GLib, Gtk

from bbport_i18n import tr
from diagnostics import LOG

REPO_CANDIDATES = [
    "SrShadowy/bloodborne_pc",
    "deadinside28/bloodborne_pc",
]


def parse_ver_tuple(ver_str: str) -> tuple[int, ...]:
    """Extracts major.minor.patch numeric tuple from version strings like 'v0.6.3' or '0.6.2-16'."""
    clean = ver_str.lstrip("v").strip().split("-")[0].split("+")[0]
    parts = []
    for p in clean.split("."):
        if p.isdigit():
            parts.append(int(p))
    return tuple(parts)


def check_for_updates_async(current_version: str, on_update_found: Optional[Callable[[str, str], None]] = None) -> None:
    """Checks for new releases asynchronously in a daemon thread.

    If a newer release is found, on_update_found(tag_name, release_url) is invoked on the GLib main thread.
    """
    def _worker():
        curr_tuple = parse_ver_tuple(current_version)
        for repo in REPO_CANDIDATES:
            url = f"https://api.github.com/repos/{repo}/releases/latest"
            try:
                req = urllib.request.Request(
                    url,
                    headers={
                        "User-Agent": f"BBPort-Launcher/{current_version}",
                        "Accept": "application/vnd.github.v3+json",
                    },
                )
                with urllib.request.urlopen(req, timeout=3.5) as resp:
                    if resp.status == 200:
                        data = json.loads(resp.read().decode("utf-8"))
                        tag_name = data.get("tag_name", "")
                        release_url = data.get("html_url", f"https://github.com/{repo}/releases/latest")
                        remote_tuple = parse_ver_tuple(tag_name)
                        if remote_tuple > curr_tuple:
                            LOG.info("updater: update available: %s (current: %s) at %s", tag_name, current_version, release_url)
                            if on_update_found:
                                GLib.idle_add(on_update_found, tag_name, release_url)
                            return
                        else:
                            LOG.debug("updater: launcher is up-to-date (%s vs %s)", current_version, tag_name)
                            return
            except Exception as e:
                LOG.debug("updater: failed checking %s (%s)", repo, e)

    t = threading.Thread(target=_worker, name="bbport-update-checker", daemon=True)
    t.start()


def notify_update_in_window(window: Adw.Window, tag_name: str, release_url: str) -> None:
    """Presents a subtle, non-blocking toast in the launcher window when an update is available."""
    if not hasattr(window, "toasts") or not window.toasts:
        return

    is_appimage = bool(os.environ.get("APPIMAGE"))
    if is_appimage:
        title = tr("Nova versão disponível (AppImage): {}").format(tag_name)
    else:
        title = tr("Nova versão disponível: {}").format(tag_name)

    toast = Adw.Toast(title=title)
    toast.set_button_label(tr("Ver no GitHub"))
    toast.set_timeout(10)

    def on_button_clicked(_toast):
        try:
            Gtk.show_uri(window, release_url, Gdk.CURRENT_TIME)
        except Exception as e:
            LOG.warning("updater: failed to launch browser: %s", e)

    toast.connect("button-clicked", on_button_clicked)
    window.toasts.add_toast(toast)
