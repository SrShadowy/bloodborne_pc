// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_video.h"
#include "platform/avplayer/player.h"

#include <mutex>

namespace BbVideoTrack {
namespace {

std::mutex g_player_mutex;
BbAvPlayer::Player* g_active_player = nullptr;

} // namespace

void OnCreate(BbAvPlayer::Player* player) {
    std::lock_guard<std::mutex> lock(g_player_mutex);
    g_active_player = player;
}

void OnClose(BbAvPlayer::Player* player) {
    std::lock_guard<std::mutex> lock(g_player_mutex);
    if (g_active_player == player) {
        g_active_player = nullptr;
    }
}

bool IsActive() {
    std::lock_guard<std::mutex> lock(g_player_mutex);
    return g_active_player && g_active_player->IsActive();
}

bool Skip() {
    std::lock_guard<std::mutex> lock(g_player_mutex);
    if (g_active_player && g_active_player->IsActive()) {
        return g_active_player->Stop();
    }
    return false;
}

} // namespace BbVideoTrack

