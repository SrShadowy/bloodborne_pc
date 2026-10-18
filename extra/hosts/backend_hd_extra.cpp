// SPDX-License-Identifier: GPL-2.0-or-later
#include "backend_hd.h"
#include "core/libraries/np/np_error.h"
#include "core/libraries/np/np_matching2/np_matching2_internal.h"

#include <chrono>

namespace Extra::Hosts {
namespace {

using namespace std::chrono_literals;

template <typename ReqT>
s32 StubSimpleRequest(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                      OrbisNpMatching2Event event, const ReqT& /*request*/) {
    ContextObject* ctx = ContextManager::Instance().Get(ctx_id);
    if (!ctx) return ORBIS_NP_MATCHING2_ERROR_INVALID_CONTEXT_ID;
    RequestCallbackInfo cb = ConsumeRequestCallback(ctx);
    ctx->request_payload.Reset();
    PendingEvent ev;
    ev.type = PendingEvent::REQUEST_CB;
    ev.ctx_id = ctx_id;
    ev.req_id = req_id;
    ev.req_event = event;
    ev.request_cb = cb.callback;
    ev.request_cb_arg = cb.arg;
    ev.error_code = ORBIS_OK;
    ev.fire_at = std::chrono::steady_clock::now() + 50ms;
    ScheduleEvent(std::move(ev));
    return ORBIS_OK;
}

}  // namespace

s32 HdBackend::GetWorldInfoList(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                                const OrbisNpMatching2GetWorldInfoListRequest& /*request*/) {
    ContextObject* ctx = ContextManager::Instance().Get(ctx_id);
    if (!ctx) return ORBIS_NP_MATCHING2_ERROR_INVALID_CONTEXT_ID;
    RequestCallbackInfo cb = ConsumeRequestCallback(ctx);

    ctx->request_payload.Reset();
    ctx->request_payload.world_info_response =
        std::make_unique<OrbisNpMatching2GetWorldInfoListResponse>();
    ctx->request_payload.world_list.resize(1);
    ctx->request_payload.world_list[0].worldId = 1;
    ctx->request_payload.world_info_response->world = ctx->request_payload.world_list.data();
    ctx->request_payload.world_info_response->worldNum = 1;

    PendingEvent ev;
    ev.type = PendingEvent::REQUEST_CB;
    ev.ctx_id = ctx_id;
    ev.req_id = req_id;
    ev.req_event = ORBIS_NP_MATCHING2_REQUEST_EVENT_GET_WORLD_INFO_LIST;
    ev.request_cb = cb.callback;
    ev.request_cb_arg = cb.arg;
    ev.error_code = ORBIS_OK;
    ev.request_data = ctx->request_payload.world_info_response.get();
    ev.fire_at = std::chrono::steady_clock::now() + 50ms;
    ScheduleEvent(std::move(ev));
    return ORBIS_OK;
}

s32 HdBackend::SearchRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                          const OrbisNpMatching2SearchRoomRequest& request, bool a_variant) {
    const auto ev = a_variant ? ORBIS_NP_MATCHING2_REQUEST_EVENT_SEARCH_ROOM_A
                              : ORBIS_NP_MATCHING2_REQUEST_EVENT_SEARCH_ROOM;
    return StubSimpleRequest(ctx_id, req_id, ev, request);
}

s32 HdBackend::GetRoomDataExternalList(
    OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
    const OrbisNpMatching2GetRoomDataExternalListRequest& request, bool a_variant) {
    const auto ev = a_variant ? ORBIS_NP_MATCHING2_REQUEST_EVENT_GET_ROOM_DATA_EXTERNAL_LIST_A
                              : ORBIS_NP_MATCHING2_REQUEST_EVENT_GET_ROOM_DATA_EXTERNAL_LIST;
    return StubSimpleRequest(ctx_id, req_id, ev, request);
}

s32 HdBackend::GetRoomMemberDataExternalList(
    OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
    const OrbisNpMatching2GetRoomMemberDataExternalListRequest& request, bool a_variant) {
    const auto ev = a_variant ? ORBIS_NP_MATCHING2_REQUEST_EVENT_GET_ROOM_MEMBER_DATA_EXTERNAL_LIST_A
                              : ORBIS_NP_MATCHING2_REQUEST_EVENT_GET_ROOM_MEMBER_DATA_EXTERNAL_LIST;
    return StubSimpleRequest(ctx_id, req_id, ev, request);
}

s32 HdBackend::GetUserInfoList(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                               const OrbisNpMatching2GetUserInfoListRequest& request,
                               bool /*a_variant*/) {
    const auto ev = ORBIS_NP_MATCHING2_REQUEST_EVENT_GET_USER_INFO_LIST;
    return StubSimpleRequest(ctx_id, req_id, ev, request);
}

s32 HdBackend::SetUserInfo(OrbisNpMatching2ContextId, OrbisNpMatching2RequestId,
                           const OrbisNpMatching2SetUserInfoRequest&) {
    return ORBIS_OK;
}

s32 HdBackend::SendRoomMessage(OrbisNpMatching2ContextId, OrbisNpMatching2RequestId,
                               const OrbisNpMatching2SendRoomMessageRequest&) {
    return ORBIS_OK;
}

s32 HdBackend::SetRoomDataInternal(OrbisNpMatching2ContextId, OrbisNpMatching2RequestId,
                                   const OrbisNpMatching2SetRoomDataInternalRequest&) {
    return ORBIS_OK;
}

s32 HdBackend::SetRoomDataExternal(OrbisNpMatching2ContextId, OrbisNpMatching2RequestId,
                                   const OrbisNpMatching2SetRoomDataExternalRequest&) {
    return ORBIS_OK;
}

}  // namespace Extra::Hosts
