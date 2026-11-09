// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the SET_ACTIVITY messages -- the JSON that carries the card to Discord (and the null one
// that clears it). The card's text is built in bbport_discord_rpc_activity.cpp, the transport in
// bbport_discord_rpc_ipc.cpp.
#include "bbport_discord_rpc_internal.h"

#include <cstdio>
#include <string>

#include "bbport_game_data.h"
#include "bbport_settings.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace BbDiscordRpc::detail {
namespace {

/// The fixed ID Discord groups one party under; the size pair is what changes.
constexpr const char* kPartyId = "ae488379-351d-4a4f-ad32-2b9b01c91657";
/// Discord ignores the nonce's value, but wants one.
constexpr const char* kNonce = "1";

} // namespace

std::string EscapeJsonString(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 16);
    for (const char c : input) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char escaped[8];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned char>(c));
                out += escaped;
            } else {
                out += c;
            }
            break;
        }
    }
    return out;
}

int CurrentProcessId() {
#ifdef _WIN32
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(getpid());
#endif
}

std::string ClearActivityJson(int pid) {
    return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) +
           ",\"activity\":null},\"nonce\":\"" + kNonce + "\"}";
}

std::string ActivityJson(int lang) {
    const BbSettings::Values& s = BbSettings::Get();
    const BbGameData::PresenceSnapshot presence = BbGameData::GetSnapshot();
    const ActivityDisplay display = FormatActivity(presence, lang);

    std::string json = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" +
                       std::to_string(CurrentProcessId()) + ",\"activity\":{";
    json += "\"details\":\"" + EscapeJsonString(display.details) + "\",";
    json += "\"state\":\"" + EscapeJsonString(display.state) + "\",";

    if (s.discord_rpc_show_time.load(std::memory_order_relaxed) &&
        presence.session_start_unix_s > 0) {
        json += "\"timestamps\":{\"start\":" + std::to_string(presence.session_start_unix_s) + "},";
    }

    json += "\"assets\":{";
    json += "\"large_image\":\"" + EscapeJsonString(display.large_image) + "\",";
    json += "\"large_text\":\"" + EscapeJsonString(display.large_text) + "\",";
    json += "\"small_image\":\"" + EscapeJsonString(display.small_image) + "\",";
    json += "\"small_text\":\"" + EscapeJsonString(display.small_text) + "\"";
    json += "}";

    if (display.has_party) {
        json += ",\"party\":{\"id\":\"" + std::string(kPartyId) + "\",\"size\":[" +
                std::to_string(display.party_size) + "," + std::to_string(display.party_max) + "]}";
    }

    json += "}},\"nonce\":\"" + std::string(kNonce) + "\"}";
    return json;
}

} // namespace BbDiscordRpc::detail
