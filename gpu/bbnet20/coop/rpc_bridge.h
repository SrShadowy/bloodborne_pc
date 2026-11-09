// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

// Phase 11, one port: the game's shadNet client (the NP sign-in, friends, scores) talked to
// shadNet's TCP port; now it connects to this bridge on 127.0.0.1, and the bridge carries the
// connection through the server link's Rpc tunnel (server/coop/rpc_tunnel.h) to shadNet, which
// hands it to its TCP port locally. A host then opens the link's UDP port only.
namespace Libraries::Net {

// Starts the bridge once (later calls give the same port): its port on 127.0.0.1, 0 when it could
// not listen. One connection at a time; a new one ends the one before.
u16 StartRpcBridge();

} // namespace Libraries::Net
