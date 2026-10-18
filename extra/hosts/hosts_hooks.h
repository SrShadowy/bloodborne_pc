// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "common/types.h"
#include "core/libraries/np/np_types.h"
#include "core/libraries/np/np_manager.h"
#include "core/libraries/np/np_matching2/np_matching2.h"
#include "core/libraries/system/userservice.h"
#include "backend.h"
#include "account.h"

#include <string_view>

namespace Libraries::Np::NpMatching2 {
struct ContextObject;
}

namespace Extra::Hosts {

using namespace Libraries::Np;
using namespace Libraries::Np::NpManager;
using namespace Libraries::Np::NpMatching2;

// --- NP Auth Hooks ---
bool HookAuthGetAuthorizationCode(s32 req_id, void* auth_code, s32* issuer_id, s32* out_res);
bool HookAuthGetIdToken(s32 req_id, void* token, s32* out_res);

// --- NP Manager Hooks ---
bool HookCheckNpAvailability(s32 req_id, OrbisNpOnlineId* online_id, s32* out_user_id);
bool HookCheckNpAvailabilityA(s32 req_id, Libraries::UserService::OrbisUserServiceUserId user_id, s32* out_res);
bool HookCheckNpReachability(s32 req_id, Libraries::UserService::OrbisUserServiceUserId user_id, s32* out_res);
bool HookCheckPlus(s32 req_id, const OrbisNpCheckPlusParameter* param, OrbisNpCheckPlusResult* result, s32* out_res);
bool HookGetAccountId(OrbisNpOnlineId* online_id, u64* account_id, s32* out_res);
bool HookGetAccountIdA(Libraries::UserService::OrbisUserServiceUserId user_id, u64* account_id, s32* out_res);
bool HookGetNpId(Libraries::UserService::OrbisUserServiceUserId user_id, OrbisNpId* np_id, s32* out_res);
bool HookGetOnlineId(Libraries::UserService::OrbisUserServiceUserId user_id, OrbisNpOnlineId* online_id, s32* out_res);
bool HookGetNpReachabilityState(Libraries::UserService::OrbisUserServiceUserId user_id, OrbisNpReachabilityState* state, s32* out_res);
bool HookGetState(Libraries::UserService::OrbisUserServiceUserId user_id, OrbisNpState* state, s32* out_res);
bool HookHasSignedUp(Libraries::UserService::OrbisUserServiceUserId user_id, bool* has_signed_up, s32* out_res);
bool HookGetUserIdByAccountId(u64 account_id, Libraries::UserService::OrbisUserServiceUserId* user_id, s32* out_res);
bool HookGetUserIdByOnlineId(const OrbisNpOnlineId* online_id, Libraries::UserService::OrbisUserServiceUserId* user_id, s32* out_res);
bool HookGetGamePresenceStatus(OrbisNpOnlineId* online_id, OrbisNpGamePresenseStatus* game_status, s32* out_res);
bool HookGetGamePresenceStatusA(Libraries::UserService::OrbisUserServiceUserId user_id, OrbisNpGamePresenseStatus* game_status, s32* out_res);
bool HookNotifyNpState(Libraries::UserService::OrbisUserServiceEventType event_type, Libraries::UserService::OrbisUserServiceUserId user_id, OrbisNpState* out_state);
bool ShouldQueueInitialSignedIn();

// --- NP WebApi Hooks ---
bool HookWebApiSendRequest(const std::string& path, char* out_data, size_t max_data,
                           size_t* out_size, int* out_http_status);

// --- NP Matching2 Hooks ---
bool HookMmContextStart(OrbisNpMatching2ContextId ctx_id);
bool HookMmContextStop(OrbisNpMatching2ContextId ctx_id, s32* out_res);
bool HookMmCreateJoinRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                          const OrbisNpMatching2CreateJoinRoomRequest& request, s32* out_res);
bool HookMmCreateJoinRoomA(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                           const OrbisNpMatching2CreateJoinRoomRequestA& request, s32* out_res);
bool HookMmJoinRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                    const OrbisNpMatching2JoinRoomRequest& request, s32* out_res);
bool HookMmJoinRoomA(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                     const OrbisNpMatching2JoinRoomRequestA& request, s32* out_res);
bool HookMmLeaveRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                     const OrbisNpMatching2LeaveRoomRequest& request, s32* out_res);
bool HookMmGetWorldInfoList(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                            const OrbisNpMatching2GetWorldInfoListRequest& request, s32* out_res);
bool HookMmSearchRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                      const OrbisNpMatching2SearchRoomRequest& request, bool a_variant, s32* out_res);
bool HookMmGetRoomDataExternalList(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                                   const OrbisNpMatching2GetRoomDataExternalListRequest& request,
                                   bool a_variant, s32* out_res);
bool HookMmGetRoomMemberDataExternalList(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                                         const OrbisNpMatching2GetRoomMemberDataExternalListRequest& request,
                                         bool a_variant, s32* out_res);
bool HookMmGetUserInfoList(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                           const OrbisNpMatching2GetUserInfoListRequest& request,
                           bool a_variant, s32* out_res);
bool HookMmSetUserInfo(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                       const OrbisNpMatching2SetUserInfoRequest& request, s32* out_res);
bool HookMmSendRoomMessage(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                           const OrbisNpMatching2SendRoomMessageRequest& request, s32* out_res);
bool HookMmSetRoomDataInternal(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                               const OrbisNpMatching2SetRoomDataInternalRequest& request, s32* out_res);
bool HookMmSetRoomDataExternal(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                               const OrbisNpMatching2SetRoomDataExternalRequest& request, s32* out_res);
bool HookMmKickoutRoomMember(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                             const OrbisNpMatching2KickoutRoomMemberRequest& request, s32* out_res);
bool HookGetServerAddr(u32* out_addr);
bool HookGetServerUdpPort(u16* out_port);
bool HookStartPeerHandshake(const ContextObject& ctx, OrbisNpMatching2RoomId room_id,
                            OrbisNpMatching2RoomMemberId member_id);
bool HookRequestSignalingInfos(std::string_view target_online_id, u32* out_addr, u16* out_port);

}  // namespace Extra::Hosts
