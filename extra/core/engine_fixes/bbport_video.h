// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace BbAvPlayer {
class Player;
}

namespace BbVideoTrack {

void OnCreate(BbAvPlayer::Player* player);
void OnClose(BbAvPlayer::Player* player);
bool IsActive();
bool Skip();

} // namespace BbVideoTrack
