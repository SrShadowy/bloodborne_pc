// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

namespace BbDiscordRpc {

/// The BBPort Discord application: presence shows Bloodborne with the map art.
inline constexpr const char* kBbportClientId = "1560890616811102219";
/// shadPS4's application, kept as an option for people who prefer that card.
inline constexpr const char* kShadps4ClientId = "1139939140494971051";

/// Activity payload display data for live preview and JSON construction.
struct ActivityDisplay {
    std::string details;
    std::string state;
    std::string large_image;
    std::string large_text;
    std::string small_image;
    std::string small_text;
    std::string time_text;
    std::string party_text;
    bool has_party = false;
    int party_size = 1;
    int party_max = 3;
};

/// Starts the Discord Rich Presence background worker thread.
void Init();

/// Stops the Discord Rich Presence thread and closes IPC connection.
void Shutdown();

/// Returns whether the client is currently connected to Discord IPC.
bool IsConnected();

/// Returns localized connection status string for display in settings UI.
const char* GetStatusText(int lang);

/// Formats current activity display data (used for preview card and JSON payload).
ActivityDisplay GetCurrentActivityDisplay(int lang);

} // namespace BbDiscordRpc
