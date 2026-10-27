#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
scripts/bump_version.py - Automates incremental build versioning for BBPort.

Versioning scheme:
    x.x.d-b (e.g. 0.6.6-8)

Rules:
    - x.x: Major.Minor prefix (kept constant during testing cycle until release).
    - d: Work days counted on this build cycle. Incremented by 1 when the date changes.
    - b: Build counter for the day. Increments with each build on the same day;
         resets to 0 when a new day begins.

Example:
    Today:    0.6.6-8  -> next build: 0.6.6-9
    Tomorrow: 0.6.6-9  -> next build: 0.6.7-0
"""

import argparse
import datetime
import os
import re
import sys
from pathlib import Path

DEFAULT_ROOT = Path(__file__).resolve().parent.parent


def get_paths(root_dir=None):
    root = Path(root_dir).resolve() if root_dir else DEFAULT_ROOT
    return {
        "root": root,
        "version": root / "VERSION",
        "build_date": root / ".build_date",
    }


def parse_version(ver_str):
    """
    Parses a version string into (prefix, day_count, build_count).
    Supports formats:
      - 0.6.6-8       -> ('0.6', 6, 8)
      - 0.6.5-dev-24  -> ('0.6', 5, 24)
      - 0.6.6         -> ('0.6', 6, 0)
      - 0.6           -> ('0.6', 0, 0)
    """
    ver = (ver_str or "").strip()
    # Match x.x.d-b or x.x.d-dev-b
    m = re.match(r"^(\d+\.\d+)\.(\d+)(?:-dev)?-(\d+)$", ver)
    if m:
        return m.group(1), int(m.group(2)), int(m.group(3))

    # Match x.x.d
    m = re.match(r"^(\d+\.\d+)\.(\d+)$", ver)
    if m:
        return m.group(1), int(m.group(2)), 0

    # Match x.x
    m = re.match(r"^(\d+\.\d+)$", ver)
    if m:
        return m.group(1), 0, 0

    # Fallback
    return "0.6", 0, 0


def get_current_version(root_dir=None):
    paths = get_paths(root_dir)
    if paths["version"].is_file():
        try:
            v = paths["version"].read_text(encoding="utf-8").strip()
            if v:
                return v
        except OSError:
            pass
    return "0.6.0-0"


def get_last_build_date(root_dir=None):
    paths = get_paths(root_dir)
    if paths["build_date"].is_file():
        try:
            content = paths["build_date"].read_text(encoding="utf-8").strip()
            if content:
                return datetime.date.fromisoformat(content.split()[0])
        except (OSError, ValueError):
            pass

    # Fallback to VERSION file modification time
    if paths["version"].is_file():
        try:
            mtime = os.path.getmtime(paths["version"])
            return datetime.date.fromtimestamp(mtime)
        except OSError:
            pass

    return datetime.date.today()


def calculate_next_version(current_version, last_date, today=None):
    if today is None:
        today = datetime.date.today()

    prefix, day_count, build_count = parse_version(current_version)

    if today == last_date:
        # Same day: increment build counter
        new_day = day_count
        new_build = build_count + 1
    elif today > last_date:
        # New day of work: increment day counter and reset build counter to 0
        new_day = day_count + 1
        new_build = 0
    else:
        # Clock skew fallback: treat as same day
        new_day = day_count
        new_build = build_count + 1

    return f"{prefix}.{new_day}-{new_build}"


def set_version(new_version, root_dir=None, target_date=None, verbose=True):
    paths = get_paths(root_dir)
    today = target_date or datetime.date.today()
    ver_clean = new_version.strip()

    paths["version"].write_text(ver_clean + "\n", encoding="utf-8")
    paths["build_date"].write_text(today.isoformat() + "\n", encoding="utf-8")

    if verbose:
        print(f"[*] Version set to: {ver_clean} ({today.isoformat()})")


def bump(root_dir=None, dry_run=False, target_date=None, verbose=True):
    paths = get_paths(root_dir)
    today = target_date or datetime.date.today()

    cur_version = get_current_version(root_dir)
    last_date = get_last_build_date(root_dir)
    next_version = calculate_next_version(cur_version, last_date, today)

    if verbose:
        if today == last_date:
            print(f"[*] Incrementing build for today ({today.isoformat()}): {cur_version} -> {next_version}")
        else:
            print(f"[*] New day detected ({last_date.isoformat()} -> {today.isoformat()}): {cur_version} -> {next_version}")

    if not dry_run:
        set_version(next_version, root_dir=root_dir, target_date=today, verbose=False)

    return next_version, cur_version


def main():
    parser = argparse.ArgumentParser(description="BBPort incremental build version manager.")
    parser.add_argument("--root", default=None, help="Root directory of repository")
    parser.add_argument("--current", action="store_true", help="Print current version and exit")
    parser.add_argument("--dry-run", action="store_true", help="Calculate next version without modifying files")
    parser.add_argument("--set", dest="set_ver", default=None, help="Explicitly set version")
    parser.add_argument("--quiet", "-q", action="store_true", help="Suppress output except errors")

    args = parser.parse_args()

    if args.current:
        print(get_current_version(args.root))
        return 0

    if args.set_ver:
        set_version(args.set_ver, root_dir=args.root, verbose=not args.quiet)
        return 0

    next_ver, _ = bump(root_dir=args.root, dry_run=args.dry_run, verbose=not args.quiet)
    if args.quiet:
        print(next_ver)
    return 0


if __name__ == "__main__":
    sys.exit(main() or 0)
