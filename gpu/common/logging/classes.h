// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: log classes (the part of the GPU library a message comes from).
#pragma once

namespace Common::Log::Class {

constexpr auto Common = "Common";
constexpr auto Common_Filesystem = "Common.Filesystem";
constexpr auto Common_Memory = "Common.Memory";
constexpr auto Config = "Config";
constexpr auto Core = "Core";
constexpr auto Debug = "Debug";
constexpr auto Frontend = "Frontend";
constexpr auto ImGui = "ImGui";
constexpr auto Input = "Input";
constexpr auto Kernel = "Kernel";
constexpr auto Kernel_Event = "Kernel.Event";
constexpr auto KeyManager = "KeyManager";
constexpr auto Lib = "Lib";
constexpr auto Lib_AvPlayer = "Lib.AvPlayer";
constexpr auto Lib_GnmDriver = "Lib.GnmDriver";
constexpr auto Lib_Kernel = "Lib.Kernel";
constexpr auto Lib_VideoOut = "Lib.VideoOut";
constexpr auto Lib_Videodec = "Lib.Videodec";
constexpr auto Loader = "Loader";
constexpr auto Log = "Log";
constexpr auto Render = "Render";
constexpr auto Render_Recompiler = "Render.Recompiler";
constexpr auto Render_Vulkan = "Render.Vulkan";
constexpr auto Tty = "Tty";

// The online module (gpu/bbnet).
constexpr auto Lib_Http = "Lib.Http";
constexpr auto Lib_Http2 = "Lib.Http2";
constexpr auto Lib_Net = "Lib.Net";
constexpr auto Lib_NetCtl = "Lib.NetCtl";
constexpr auto Lib_NpAuth = "Lib.NpAuth";
constexpr auto Lib_NpCommerce = "Lib.NpCommerce";
constexpr auto Lib_NpCommon = "Lib.NpCommon";
constexpr auto Lib_NpManager = "Lib.NpManager";
constexpr auto Lib_NpMatching = "Lib.NpMatching";
constexpr auto Lib_NpMatching2 = "Lib.NpMatching2";
constexpr auto Lib_NpPartner = "Lib.NpPartner";
constexpr auto Lib_NpParty = "Lib.NpParty";
constexpr auto Lib_NpProfileDialog = "Lib.NpProfileDialog";
constexpr auto Lib_NpScore = "Lib.NpScore";
constexpr auto Lib_NpSignaling = "Lib.NpSignaling";
constexpr auto Lib_NpSnsFacebookDialog = "Lib.NpSnsFacebookDialog";
constexpr auto Lib_NpTrophy = "Lib.NpTrophy";
constexpr auto Lib_NpTus = "Lib.NpTus";
constexpr auto Lib_NpWebApi = "Lib.NpWebApi";
constexpr auto Lib_NpWebApi2 = "Lib.NpWebApi2";
constexpr auto Lib_Ssl = "Lib.Ssl";
constexpr auto Lib_Ssl2 = "Lib.Ssl2";
constexpr auto NpHandler = "NpHandler";
constexpr auto ShadNet = "ShadNet";

} // namespace Common::Log::Class
