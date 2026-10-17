// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

namespace BbDiscordRpc {

/// Starts the Discord Rich Presence background worker thread.
void Init();

/// Stops the Discord Rich Presence thread and closes IPC connection.
void Shutdown();

/// Returns whether the client is currently connected to Discord IPC.
bool IsConnected();

/// Returns localized connection status string for display in settings UI.
const char* GetStatusText(int lang);

} // namespace BbDiscordRpc
