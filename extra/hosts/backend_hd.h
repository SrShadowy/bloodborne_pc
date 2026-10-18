// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <nlohmann/json.hpp>
#include "backend.h"

namespace Extra::Hosts {

class HdBackend final : public OnlineBackend {
public:
    HdBackend();
    ~HdBackend() override;

    void ContextStart(OrbisNpMatching2ContextId ctx_id) override;
    s32 ContextStop(OrbisNpMatching2ContextId ctx_id) override;

    s32 CreateJoinRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                       const OrbisNpMatching2CreateJoinRoomRequest& request) override;
    s32 CreateJoinRoomA(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                        const OrbisNpMatching2CreateJoinRoomRequestA& request) override;

    s32 JoinRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                 const OrbisNpMatching2JoinRoomRequest& request) override;
    s32 JoinRoomA(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                  const OrbisNpMatching2JoinRoomRequestA& request) override;

    s32 LeaveRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                  const OrbisNpMatching2LeaveRoomRequest& request) override;

    s32 GetWorldInfoList(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                         const OrbisNpMatching2GetWorldInfoListRequest& request) override;

    s32 SearchRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                   const OrbisNpMatching2SearchRoomRequest& request, bool a_variant) override;

    s32 GetRoomDataExternalList(
        OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
        const OrbisNpMatching2GetRoomDataExternalListRequest& request, bool a_variant) override;

    s32 GetRoomMemberDataExternalList(
        OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
        const OrbisNpMatching2GetRoomMemberDataExternalListRequest& request, bool a_variant) override;

    s32 GetUserInfoList(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                        const OrbisNpMatching2GetUserInfoListRequest& request,
                        bool a_variant) override;

    s32 SetUserInfo(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                    const OrbisNpMatching2SetUserInfoRequest& request) override;

    s32 SendRoomMessage(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                        const OrbisNpMatching2SendRoomMessageRequest& request) override;

    s32 SetRoomDataInternal(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                            const OrbisNpMatching2SetRoomDataInternalRequest& request) override;

    s32 SetRoomDataExternal(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                            const OrbisNpMatching2SetRoomDataExternalRequest& request) override;

    s32 KickoutRoomMember(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                          const OrbisNpMatching2KickoutRoomMemberRequest& request) override;

    u32 GetServerAddr() override;
    u16 GetServerUdpPort() override;

    bool RequestSignalingInfos(std::string_view target_online_id, u32* out_addr,
                               u16* out_port) override;

private:
    void HandleServerEvent(const nlohmann::json& ev);
};

}  // namespace Extra::Hosts
