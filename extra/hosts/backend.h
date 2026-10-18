// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <memory>
#include <string_view>

#include "common/types.h"
#include "core/libraries/np/np_matching2/np_matching2.h"

namespace Extra::Hosts {

using namespace Libraries::Np::NpMatching2;

class OnlineBackend {
public:
    virtual ~OnlineBackend() = default;

    virtual void ContextStart(OrbisNpMatching2ContextId ctx_id) = 0;
    virtual s32 ContextStop(OrbisNpMatching2ContextId ctx_id) = 0;

    virtual s32 CreateJoinRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                               const OrbisNpMatching2CreateJoinRoomRequest& request) = 0;
    virtual s32 CreateJoinRoomA(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                                const OrbisNpMatching2CreateJoinRoomRequestA& request) = 0;

    virtual s32 JoinRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                         const OrbisNpMatching2JoinRoomRequest& request) = 0;
    virtual s32 JoinRoomA(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                          const OrbisNpMatching2JoinRoomRequestA& request) = 0;

    virtual s32 LeaveRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                          const OrbisNpMatching2LeaveRoomRequest& request) = 0;

    virtual s32 GetWorldInfoList(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                                 const OrbisNpMatching2GetWorldInfoListRequest& request) = 0;

    virtual s32 SearchRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                           const OrbisNpMatching2SearchRoomRequest& request, bool a_variant) = 0;

    virtual s32 GetRoomDataExternalList(
        OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
        const OrbisNpMatching2GetRoomDataExternalListRequest& request, bool a_variant) = 0;

    virtual s32 GetRoomMemberDataExternalList(
        OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
        const OrbisNpMatching2GetRoomMemberDataExternalListRequest& request, bool a_variant) = 0;

    virtual s32 GetUserInfoList(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                                const OrbisNpMatching2GetUserInfoListRequest& request,
                                bool a_variant) = 0;

    virtual s32 SetUserInfo(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                            const OrbisNpMatching2SetUserInfoRequest& request) = 0;

    virtual s32 SendRoomMessage(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                                const OrbisNpMatching2SendRoomMessageRequest& request) = 0;

    virtual s32 SetRoomDataInternal(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                                    const OrbisNpMatching2SetRoomDataInternalRequest& request) = 0;

    virtual s32 SetRoomDataExternal(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                                    const OrbisNpMatching2SetRoomDataExternalRequest& request) = 0;

    virtual s32 KickoutRoomMember(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                                  const OrbisNpMatching2KickoutRoomMemberRequest& request) = 0;

    virtual u32 GetServerAddr() = 0;
    virtual u16 GetServerUdpPort() = 0;

    virtual bool RequestSignalingInfos(std::string_view target_online_id, u32* out_addr,
                                       u16* out_port) = 0;
};

// Returns whether a non-shadNet custom host backend is active.
// Active when BB_ONLINE_BACKEND is set to "hd", "thehuntersdream", or "bbhost".
bool IsCustomHostActive();

// Returns the active custom host backend instance.
OnlineBackend& GetActiveBackend();

}  // namespace Extra::Hosts
