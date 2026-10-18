// SPDX-License-Identifier: GPL-2.0-or-later
#include "hosts_hooks.h"
#include "backend.h"

namespace Extra::Hosts {

using namespace Libraries::Np;
using namespace Libraries::Np::NpMatching2;

// --- NP Matching2 Hooks ---

bool HookMmContextStart(OrbisNpMatching2ContextId ctx_id) {
    if (!IsCustomHostActive()) return false;
    GetActiveBackend().ContextStart(ctx_id);
    return true;
}

bool HookMmContextStop(OrbisNpMatching2ContextId ctx_id, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    s32 res = GetActiveBackend().ContextStop(ctx_id);
    if (out_res) *out_res = res;
    return true;
}

bool HookMmCreateJoinRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                          const OrbisNpMatching2CreateJoinRoomRequest& request, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    s32 res = GetActiveBackend().CreateJoinRoom(ctx_id, req_id, request);
    if (out_res) *out_res = res;
    return true;
}

bool HookMmCreateJoinRoomA(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                           const OrbisNpMatching2CreateJoinRoomRequestA& request, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    s32 res = GetActiveBackend().CreateJoinRoomA(ctx_id, req_id, request);
    if (out_res) *out_res = res;
    return true;
}

bool HookMmJoinRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                    const OrbisNpMatching2JoinRoomRequest& request, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    s32 res = GetActiveBackend().JoinRoom(ctx_id, req_id, request);
    if (out_res) *out_res = res;
    return true;
}

bool HookMmJoinRoomA(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                     const OrbisNpMatching2JoinRoomRequestA& request, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    s32 res = GetActiveBackend().JoinRoomA(ctx_id, req_id, request);
    if (out_res) *out_res = res;
    return true;
}

bool HookMmLeaveRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                     const OrbisNpMatching2LeaveRoomRequest& request, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    s32 res = GetActiveBackend().LeaveRoom(ctx_id, req_id, request);
    if (out_res) *out_res = res;
    return true;
}

bool HookMmGetWorldInfoList(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                            const OrbisNpMatching2GetWorldInfoListRequest& request, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    s32 res = GetActiveBackend().GetWorldInfoList(ctx_id, req_id, request);
    if (out_res) *out_res = res;
    return true;
}

bool HookMmSearchRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                      const OrbisNpMatching2SearchRoomRequest& request, bool a_variant, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    s32 res = GetActiveBackend().SearchRoom(ctx_id, req_id, request, a_variant);
    if (out_res) *out_res = res;
    return true;
}

bool HookMmGetRoomDataExternalList(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                                   const OrbisNpMatching2GetRoomDataExternalListRequest& request,
                                   bool a_variant, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    s32 res = GetActiveBackend().GetRoomDataExternalList(ctx_id, req_id, request, a_variant);
    if (out_res) *out_res = res;
    return true;
}

bool HookMmGetRoomMemberDataExternalList(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                                         const OrbisNpMatching2GetRoomMemberDataExternalListRequest& request,
                                         bool a_variant, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    s32 res = GetActiveBackend().GetRoomMemberDataExternalList(ctx_id, req_id, request, a_variant);
    if (out_res) *out_res = res;
    return true;
}

bool HookMmGetUserInfoList(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                           const OrbisNpMatching2GetUserInfoListRequest& request,
                           bool a_variant, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    s32 res = GetActiveBackend().GetUserInfoList(ctx_id, req_id, request, a_variant);
    if (out_res) *out_res = res;
    return true;
}

bool HookMmSetUserInfo(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                       const OrbisNpMatching2SetUserInfoRequest& request, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    s32 res = GetActiveBackend().SetUserInfo(ctx_id, req_id, request);
    if (out_res) *out_res = res;
    return true;
}

bool HookMmSendRoomMessage(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                           const OrbisNpMatching2SendRoomMessageRequest& request, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    s32 res = GetActiveBackend().SendRoomMessage(ctx_id, req_id, request);
    if (out_res) *out_res = res;
    return true;
}

bool HookMmSetRoomDataInternal(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                               const OrbisNpMatching2SetRoomDataInternalRequest& request, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    s32 res = GetActiveBackend().SetRoomDataInternal(ctx_id, req_id, request);
    if (out_res) *out_res = res;
    return true;
}

bool HookMmSetRoomDataExternal(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                               const OrbisNpMatching2SetRoomDataExternalRequest& request, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    s32 res = GetActiveBackend().SetRoomDataExternal(ctx_id, req_id, request);
    if (out_res) *out_res = res;
    return true;
}

bool HookMmKickoutRoomMember(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                             const OrbisNpMatching2KickoutRoomMemberRequest& request, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    s32 res = GetActiveBackend().KickoutRoomMember(ctx_id, req_id, request);
    if (out_res) *out_res = res;
    return true;
}

bool HookGetServerAddr(u32* out_addr) {
    if (!IsCustomHostActive()) return false;
    if (out_addr) *out_addr = GetActiveBackend().GetServerAddr();
    return true;
}

bool HookGetServerUdpPort(u16* out_port) {
    if (!IsCustomHostActive()) return false;
    if (out_port) *out_port = GetActiveBackend().GetServerUdpPort();
    return true;
}

bool HookStartPeerHandshake(const ContextObject& /*ctx*/, OrbisNpMatching2RoomId /*room_id*/,
                            OrbisNpMatching2RoomMemberId /*member_id*/) {
    return IsCustomHostActive();
}

bool HookRequestSignalingInfos(std::string_view target_online_id, u32* out_addr, u16* out_port) {
    if (!IsCustomHostActive()) return false;
    return GetActiveBackend().RequestSignalingInfos(target_online_id, out_addr, out_port);
}

}  // namespace Extra::Hosts
