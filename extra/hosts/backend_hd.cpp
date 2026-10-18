// SPDX-License-Identifier: GPL-2.0-or-later
#include "backend_hd.h"
#include "account.h"
#include "backend_hd_payload.h"
#include "core/libraries/np/np_error.h"
#include "core/libraries/np/np_matching2/np_matching2_internal.h"
#include "http_client.h"
#include "session_poll.h"
#include "session_room.h"
#include "session_state.h"

#include <chrono>

namespace Extra::Hosts {
namespace {

using namespace std::chrono_literals;

}  // namespace

HdBackend::HdBackend() = default;
HdBackend::~HdBackend() = default;

void HdBackend::HandleServerEvent(const nlohmann::json& ev) {
    const std::string name = StrOf(ev, "Name");
    const auto now = std::chrono::steady_clock::now();

    if (name == "room_member_joined") {
        Peer p;
        p.member_id = static_cast<uint16_t>(IntOf(ev, "MemberId", 0));
        p.online_id = StrOf(ev, "OnlineId");
        p.addr = AddrOf(StrOf(ev, "Addr"));
        p.port = static_cast<uint16_t>(IntOf(ev, "Port", 0));
        PeersUpsert(p);

        PendingEvent rev;
        rev.type = PendingEvent::ROOM_EVENT_CB;
        rev.ctx_id = 1;
        rev.room_id = CurrentRoomId();
        rev.room_event = ORBIS_NP_MATCHING2_ROOM_EVENT_MEMBER_JOINED;
        rev.fire_at = now + 100ms;
        ScheduleEvent(std::move(rev));

        PendingEvent sev;
        sev.type = PendingEvent::SIGNALING_CB;
        sev.ctx_id = 1;
        sev.room_id = CurrentRoomId();
        sev.member_id = p.member_id;
        sev.sig_event = ORBIS_NP_MATCHING2_SIGNALING_EVENT_ESTABLISHED;
        sev.error_code = ORBIS_OK;
        sev.fire_at = now + 500ms;
        ScheduleEvent(std::move(sev));
    } else if (name == "room_member_left") {
        const uint16_t mid = static_cast<uint16_t>(IntOf(ev, "MemberId", 0));
        PeersErase(mid);

        PendingEvent rev;
        rev.type = PendingEvent::ROOM_EVENT_CB;
        rev.ctx_id = 1;
        rev.room_id = CurrentRoomId();
        rev.room_event = ORBIS_NP_MATCHING2_ROOM_EVENT_MEMBER_LEFT;
        rev.fire_at = now + 100ms;
        ScheduleEvent(std::move(rev));
    }
}

void HdBackend::ContextStart(OrbisNpMatching2ContextId ctx_id) {
    nlohmann::json reply;
    std::string err;
    (void)ServerContextStart(reply, err);

    PollerStart([this](const nlohmann::json& ev) {
        HandleServerEvent(ev);
    });

    PendingEvent ev;
    ev.ctx_id = ctx_id;
    ev.type = PendingEvent::CONTEXT_CB;
    ev.ctx_event = ORBIS_NP_MATCHING2_CONTEXT_EVENT_STARTED;
    ev.ctx_event_cause = ORBIS_NP_MATCHING2_EVENT_CAUSE_CONTEXT_ACTION;
    ev.error_code = ORBIS_OK;
    ev.fire_at = std::chrono::steady_clock::now() + 200ms;
    ScheduleEvent(std::move(ev));
}

s32 HdBackend::ContextStop(OrbisNpMatching2ContextId ctx_id) {
    PollerStop();
    RoomClear();

    PendingEvent ev;
    ev.ctx_id = ctx_id;
    ev.type = PendingEvent::CONTEXT_CB;
    ev.ctx_event = ORBIS_NP_MATCHING2_CONTEXT_EVENT_STOPPED;
    ev.ctx_event_cause = ORBIS_NP_MATCHING2_EVENT_CAUSE_CONTEXT_ACTION;
    ev.error_code = ORBIS_OK;
    ev.fire_at = std::chrono::steady_clock::now();
    ScheduleEvent(std::move(ev));
    return ORBIS_OK;
}

s32 HdBackend::CreateJoinRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                              const OrbisNpMatching2CreateJoinRoomRequest& request) {
    ContextObject* ctx = ContextManager::Instance().Get(ctx_id);
    if (!ctx) return ORBIS_NP_MATCHING2_ERROR_INVALID_CONTEXT_ID;

    RequestCallbackInfo cb_info = ConsumeRequestCallback(ctx);
    nlohmann::json reply;
    std::string error;
    const bool ok = ServerCreateRoom(request.maxSlot, {}, reply, error);

    PendingEvent ev;
    ev.type = PendingEvent::REQUEST_CB;
    ev.ctx_id = ctx_id;
    ev.req_id = req_id;
    ev.req_event = ORBIS_NP_MATCHING2_REQUEST_EVENT_CREATE_JOIN_ROOM;
    ev.request_cb = cb_info.callback;
    ev.request_cb_arg = cb_info.arg;
    ev.fire_at = std::chrono::steady_clock::now() + 300ms;

    if (ok) {
        const uint64_t room_id = static_cast<uint64_t>(IntOf(reply, "RoomId", 1));
        const uint16_t me_id = static_cast<uint16_t>(IntOf(reply, "MemberId", 1));
        const uint16_t owner_id = static_cast<uint16_t>(IntOf(reply, "OwnerMemberId", me_id));
        PopulateCreateJoinRoomPayload(*ctx, room_id, me_id, owner_id);
        ev.error_code = ORBIS_OK;
        ev.request_data = ctx->request_payload.create_join_response.get();
    } else {
        ev.error_code = ORBIS_NP_MATCHING2_ERROR_SERVER_NOT_AVAILABLE;
        ev.request_data = nullptr;
    }
    ScheduleEvent(std::move(ev));
    return ORBIS_OK;
}

s32 HdBackend::CreateJoinRoomA(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                               const OrbisNpMatching2CreateJoinRoomRequestA& request) {
    OrbisNpMatching2CreateJoinRoomRequest r{};
    r.maxSlot = request.maxSlot;
    return CreateJoinRoom(ctx_id, req_id, r);
}

s32 HdBackend::JoinRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                        const OrbisNpMatching2JoinRoomRequest& request) {
    ContextObject* ctx = ContextManager::Instance().Get(ctx_id);
    if (!ctx) return ORBIS_NP_MATCHING2_ERROR_INVALID_CONTEXT_ID;

    RequestCallbackInfo cb_info = ConsumeRequestCallback(ctx);
    nlohmann::json reply;
    std::string error;
    const bool ok = ServerJoinRoom(request.roomId, reply, error);

    PendingEvent ev;
    ev.type = PendingEvent::REQUEST_CB;
    ev.ctx_id = ctx_id;
    ev.req_id = req_id;
    ev.req_event = ORBIS_NP_MATCHING2_REQUEST_EVENT_JOIN_ROOM;
    ev.request_cb = cb_info.callback;
    ev.request_cb_arg = cb_info.arg;
    ev.fire_at = std::chrono::steady_clock::now() + 300ms;

    if (ok) {
        const uint16_t me_id = static_cast<uint16_t>(IntOf(reply, "MemberId", 2));
        const uint16_t owner_id = static_cast<uint16_t>(IntOf(reply, "OwnerMemberId", 1));
        PopulateCreateJoinRoomPayload(*ctx, request.roomId, me_id, owner_id);
        ev.error_code = ORBIS_OK;
        ev.request_data = ctx->request_payload.create_join_response.get();
    } else {
        ev.error_code = ORBIS_NP_MATCHING2_ERROR_SERVER_NOT_AVAILABLE;
        ev.request_data = nullptr;
    }
    ScheduleEvent(std::move(ev));
    return ORBIS_OK;
}

s32 HdBackend::JoinRoomA(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                         const OrbisNpMatching2JoinRoomRequestA& request) {
    OrbisNpMatching2JoinRoomRequest r{};
    r.roomId = request.roomId;
    return JoinRoom(ctx_id, req_id, r);
}

s32 HdBackend::LeaveRoom(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                         const OrbisNpMatching2LeaveRoomRequest& /*request*/) {
    ContextObject* ctx = ContextManager::Instance().Get(ctx_id);
    if (!ctx) return ORBIS_NP_MATCHING2_ERROR_INVALID_CONTEXT_ID;

    RequestCallbackInfo cb_info = ConsumeRequestCallback(ctx);
    nlohmann::json reply;
    std::string error;
    (void)ServerLeaveRoom(CurrentSessionId(), CurrentMemberId(), reply, error);

    ctx->request_payload.Reset();
    ctx->request_payload.leave_room_response =
        std::make_unique<OrbisNpMatching2LeaveRoomResponse>();
    ctx->request_payload.leave_room_response->roomId = ctx->room_id;

    PendingEvent ev;
    ev.type = PendingEvent::REQUEST_CB;
    ev.ctx_id = ctx_id;
    ev.req_id = req_id;
    ev.req_event = ORBIS_NP_MATCHING2_REQUEST_EVENT_LEAVE_ROOM;
    ev.request_cb = cb_info.callback;
    ev.request_cb_arg = cb_info.arg;
    ev.error_code = ORBIS_OK;
    ev.request_data = ctx->request_payload.leave_room_response.get();
    ev.fire_at = std::chrono::steady_clock::now() + 300ms;
    ScheduleEvent(std::move(ev));
    return ORBIS_OK;
}

s32 HdBackend::KickoutRoomMember(OrbisNpMatching2ContextId ctx_id, OrbisNpMatching2RequestId req_id,
                                 const OrbisNpMatching2KickoutRoomMemberRequest& request) {
    std::string err;
    (void)ServerKickMember(CurrentSessionId(), request.memberId,
                           CurrentMemberId(), request.optData.data,
                           request.optData.len, err);
    ContextObject* ctx = ContextManager::Instance().Get(ctx_id);
    if (!ctx) return ORBIS_OK;
    RequestCallbackInfo cb = ConsumeRequestCallback(ctx);
    PendingEvent ev;
    ev.type = PendingEvent::REQUEST_CB;
    ev.ctx_id = ctx_id;
    ev.req_id = req_id;
    ev.req_event = ORBIS_NP_MATCHING2_REQUEST_EVENT_KICKOUT_ROOM_MEMBER;
    ev.request_cb = cb.callback;
    ev.request_cb_arg = cb.arg;
    ev.error_code = ORBIS_OK;
    ev.fire_at = std::chrono::steady_clock::now() + 100ms;
    ScheduleEvent(std::move(ev));
    return ORBIS_OK;
}

u32 HdBackend::GetServerAddr() {
    return AddrOf("127.0.0.1");
}

u16 HdBackend::GetServerUdpPort() {
    return static_cast<u16>(P2pPort());
}

bool HdBackend::RequestSignalingInfos(std::string_view target_online_id, u32* out_addr,
                                      u16* out_port) {
    const std::string id_str(target_online_id);
    Peer p;
    if (PeersFindOnline(id_str, &p) && p.addr != 0 && p.port != 0) {
        if (out_addr) *out_addr = p.addr;
        if (out_port) *out_port = p.port;
        return true;
    }
    std::string err;
    return ServerSignalingResolve(id_str, out_addr, out_port, err);
}

}  // namespace Extra::Hosts
