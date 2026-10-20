// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Central re-export of all extra hooks per MyGUIDELINES.md.
#if __has_include("hosts/hosts_hooks.h") && __has_include("core/libraries/np/np_types.h")
#include "hosts/hosts_hooks.h"
#endif
#include "core/frame_pacing/bbport_present_pacer.h"
#include "core/frame_pacing/bbport_photo_pause.h"
#include "core/debugger/mem_map.h"
#include "core/bbsc/bbport_bbsc_kit.h"
