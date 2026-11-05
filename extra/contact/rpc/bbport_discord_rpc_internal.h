// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the Discord Rich Presence files share (bbport_discord_rpc.cpp: the worker;
// bbport_discord_rpc_activity.cpp: the card's text; bbport_discord_rpc_json.cpp: the SET_ACTIVITY
// messages; bbport_discord_rpc_ipc.cpp: the pipe/socket). Only this module needs it.
#pragma once

#include <cstdint>
#include <string>

#include "bbport_discord_rpc.h"
#include "bbport_game_data.h"
#include "bbport_settings.h"

namespace BbDiscordRpc::detail {

/// The frame Discord's IPC uses: an opcode and the length of the JSON that follows it.
#pragma pack(push, 1)
struct IpcHeader {
    std::uint32_t opcode = 0;
    std::uint32_t length = 0;
};
#pragma pack(pop)

/// The connection to the Discord client. A named pipe on Windows, a Unix socket elsewhere.
struct IpcConnection {
#ifdef _WIN32
    void* handle = nullptr; ///< a HANDLE, kept as void* so this header stays platform-free
#else
    int fd = -1;
#endif

    bool IsOpen() const;
    void Close();
};

/// Connects to the running Discord client, walking discord-ipc-0..9. Never blocks for long; an
/// unopened connection comes back closed.
IpcConnection Connect();

/// Announces the application ID; true when Discord answered READY.
bool PerformHandshake(IpcConnection& connection);

/// Sends one framed message. False when the client went away (the caller then reconnects).
bool Send(const IpcConnection& connection, std::uint32_t opcode, const std::string& json);

/// Reads one framed message. False when the client went away.
bool Read(IpcConnection& connection, std::uint32_t& opcode, std::string& json);

/// True when a complete frame is waiting, so Read() will not block.
bool HasPendingFrame(const IpcConnection& connection);

/// The application ID the presence is announced with: the setting, or BBPort's own.
std::string ClientId();

/// The JSON that clears the presence (activity: null), used when RPC is disabled or bbport exits.
std::string ClearActivityJson(int pid);

/// This process's ID, which SET_ACTIVITY identifies the client by.
int CurrentProcessId();

/// The "SET_ACTIVITY" message for the current game state.
std::string ActivityJson(int lang);

/// Fills the display fields the settings window's preview card shows.
ActivityDisplay FormatActivity(int lang);

/// The same for a snapshot the caller already has (bbport_discord_rpc_activity.cpp).
ActivityDisplay FormatActivity(const BbGameData::PresenceSnapshot& presence, int lang);

/// Escapes the characters JSON strings forbid (quotes, backslashes, control characters).
std::string EscapeJsonString(const std::string& input);

/// The artwork fields of the in-game card (bbport_discord_rpc_art.cpp).
void FillAssets(const BbGameData::PresenceSnapshot& presence, const BbSettings::Values& s, int lang,
                ActivityDisplay& display);

/// The artwork fields before a save is loaded (bbport_discord_rpc_art.cpp).
void FillMainMenuAssets(const BbSettings::Values& s, ActivityDisplay& display);
} // namespace BbDiscordRpc::detail
