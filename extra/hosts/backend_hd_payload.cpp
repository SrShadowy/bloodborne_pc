// SPDX-License-Identifier: GPL-2.0-or-later
#include "backend_hd_payload.h"
#include "account.h"
#include "session_state.h"
#include "core/libraries/np/np_types.h"

#include <vector>

namespace Extra::Hosts {

using namespace Libraries::Np::NpMatching2;

void PopulateCreateJoinRoomPayload(ContextObject& ctx, uint64_t room_id, uint16_t me_id,
                                   uint16_t owner_id) {
    CallbackPayload& p = ctx.request_payload;
    p.Reset();
    p.room_data = std::make_unique<OrbisNpMatching2RoomDataInternal>();
    auto& rd = *p.room_data;
    rd.roomId = room_id;
    rd.maxSlot = 3;
    rd.publicSlots = 3;
    rd.openPublicSlots = 2;
    rd.flags = 0;

    ctx.room_id = room_id;
    ctx.my_member_id = me_id;
    ctx.is_room_owner = (me_id == owner_id);

    RoomCache& rc = ctx.room_cache[room_id];
    rc = RoomCache{};
    rc.room_id = room_id;
    rc.owner = ctx.is_room_owner;
    rc.signaling_main_member = owner_id;

    std::vector<Peer> peers = PeersAll();
    bool have_me = false;
    for (const auto& peer : peers) {
        if (peer.member_id == me_id) have_me = true;
    }
    if (!have_me) {
        Peer me;
        me.member_id = me_id;
        me.online_id = GetOnlineId();
        me.addr = AddrOf(LocalAddrText());
        me.port = static_cast<uint16_t>(P2pPort());
        peers.push_back(me);
    }

    const size_t member_count = peers.size();
    p.member_data.resize(member_count);
    for (size_t i = 0; i < member_count; ++i) {
        const auto& peer = peers[i];
        auto& dst = p.member_data[i];
        dst = OrbisNpMatching2RoomMemberDataInternal{};
        dst.next = (i + 1 < member_count) ? &p.member_data[i + 1] : nullptr;
        dst.joinDate = 0;
        SetNpId(dst.npId, peer.online_id);
        dst.memberId = peer.member_id;
        dst.teamId = 0;
        dst.natType = 1;
        dst.flagAttr = (peer.member_id == owner_id)
                           ? ORBIS_NP_MATCHING2_ROOMMEMBER_FLAG_ATTR_OWNER
                           : 0;

        MemberCache mc{};
        mc.member_id = peer.member_id;
        mc.addr = peer.addr;
        mc.port = peer.port;
        SetNpId(mc.np_id, peer.online_id);
        if (peer.member_id == owner_id) {
            mc.flag_attr |= ORBIS_NP_MATCHING2_ROOMMEMBER_FLAG_ATTR_OWNER;
        }
        rc.members[peer.member_id] = mc;
    }

    p.create_join_response = std::make_unique<OrbisNpMatching2CreateJoinRoomResponse>();
    auto& out = *p.create_join_response;
    out.roomData = p.room_data.get();
    out.members.members = p.member_data.empty() ? nullptr : p.member_data.data();
    out.members.membersNum = p.member_data.size();
    out.members.me = nullptr;
    out.members.owner = nullptr;
    for (auto& md : p.member_data) {
        if (md.memberId == me_id) {
            out.members.me = &md;
        }
        if (md.memberId == owner_id) {
            out.members.owner = &md;
        }
    }
}

}  // namespace Extra::Hosts
