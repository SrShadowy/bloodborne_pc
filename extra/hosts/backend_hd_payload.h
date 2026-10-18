// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include "core/libraries/np/np_matching2/np_matching2_internal.h"

namespace Extra::Hosts {

// Populates ContextObject's request_payload with CreateJoinRoom response structures
// derived from custom host peer table and server room response.
void PopulateCreateJoinRoomPayload(Libraries::Np::NpMatching2::ContextObject& ctx,
                                   uint64_t room_id, uint16_t me_id, uint16_t owner_id);

}  // namespace Extra::Hosts
