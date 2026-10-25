// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Central re-export of all extra hooks
#if __has_include("hosts/hosts_hooks.h") && __has_include("core/libraries/np/np_types.h")
#include "hosts/hosts_hooks.h"
#endif
#include "core/frame_pacing/bbport_present_pacer.h"
#include "core/frame_pacing/bbport_photo_pause.h"
#include "core/video_backend/bbport_video_backend.h"
#include "core/debugger/mem_map.h"
#include "core/bbsc/bbport_bbsc_kit.h"
#include "core/shader_precompile/bbport_shader_discovery.h"
#include "core/shader_precompile/bbport_shader_pack.h"
#include "core/shader_precompile/bbport_vk_pipeline_cache.h"
#include "ui/scaleform/bbport_title_menu.h"

#ifdef __cplusplus
#include "vulkan/vk_common.h"
void extra_fg_set_display_lut(vk::ImageView lut);
vk::ImageView extra_fg_get_display_lut();
bool extra_shader_pack_restore_master();
#endif
