// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Intercepts guest reads to /menu/title*.gfx and transparently expands
/// the title menu CommandList to 6 rows in memory so Exit Game does not scroll.
int extra_title_menu_file(const char *guest, int host);

#ifdef __cplusplus
}
#endif
