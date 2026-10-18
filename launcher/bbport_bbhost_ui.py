# SPDX-License-Identifier: GPL-2.0-or-later
"""Compatibility forwarding module: redirects bbport_bbhost_ui to bbport_hosts_ui."""
import sys
import bbport_hosts_ui

sys.modules[__name__] = bbport_hosts_ui
