# SPDX-License-Identifier: GPL-2.0-or-later
"""Compatibility forwarding module: redirects bbport_bbhost to bbport_hosts."""
import sys
import bbport_hosts

sys.modules[__name__] = bbport_hosts
