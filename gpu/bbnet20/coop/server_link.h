// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

#include "common/types.h"

// The server link (net_link.h): in the port (always) every
// frame the P2P transport sends to a peer goes through shadNet, and the party's world and boards
// live there (phase 3).
namespace Libraries::Net {

// The port's co-op on the server (the team's world, boards and game packet link); shadPS4 keeps
// its own co-op, but its peers too are reached through the server only (phase 9).
bool ServerLinkSwitchOn();
// shadNet's link UDP port, this game's online id and (phase 11) its secret: the link's hello
// signs the player in. Link version 4: the server's public key (64 hex digits, the launcher's
// BB_SHADNET_SERVER_KEY); none, or not a key: the first cookie's, kept once a welcome proves it.
void ConfigureServerLink(u32 server_addr_nbo, u16 server_port_nbo, std::string online_id,
                         std::string secret = {}, std::string_view server_key = {});
// Which game this is, for the hello: shadNet refuses a game on another build.
void SetServerLinkBuild(std::string build);
// The link's events ("welcome", "refused", "differs", "quiet", "back", "member-quiet",
// "member-back", "member-gone", "rtt") for the co-op trace.
using ServerLinkObserver = void (*)(std::string_view event, std::string_view detail);
void SetServerLinkObserver(ServerLinkObserver observer);
// Our messages with shadNet itself (the link's Coop kind, phase 3): one stream frame's body each
// way. Send is false while there is no link (no server known, the transport not started); Receive
// gives one body (its size, -1 when none waits).
bool ServerCoopSend(const void* body, u32 len);
int ServerCoopReceive(void* body, u32 len);
// Phase 11: the Rpc tunnel's frames (server/coop/rpc_tunnel.h), the same way. Send works before
// the server's welcome (the server drops them until then; the stream sends them again).
bool ServerRpcSend(const void* body, u32 len);
int ServerRpcReceive(void* body, u32 len);
// Peer P2P frames routed through the server (Link::Kind::Data).
bool ServerDataSend(u32 dest_addr_nbo, u16 dest_port_nbo, const void* frame, u32 len);
int ServerDataReceive(void* frame, u32 len, u32* from_addr_nbo, u16* from_port_nbo);
// Whether the server took this game's hello (it may restart: then false until it does again).
bool ServerLinkWelcomed();
// Phase 7 (net_link.h ConnectsWithoutHandshake): room members are connected at once through the
// server once the link is configured; nothing is ever connected straight.
bool ConnectsPeersWithoutHandshake();
// The rig's dropped connection (autoplay cut-link, phase 7): nothing goes out or comes in on this
// game's UDP socket for `ms`, as when its Wi-Fi drops.
void CutServerLinkForTest(u32 ms);
// Why the server refused this game's hello (another release), empty while it did not.
std::string ServerLinkRefusal();
// What the server's welcome warned of (another installation than the others': the team screen's
// line, then every field), empty when nothing differs.
std::string ServerLinkWarning();

} // namespace Libraries::Net
