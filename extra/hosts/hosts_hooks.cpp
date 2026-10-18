// SPDX-License-Identifier: GPL-2.0-or-later
#include "hosts_hooks.h"
#include "backend.h"
#include "account.h"
#include "core/libraries/np/np_error.h"

#include <cstring>
#include <functional>

namespace Extra::Hosts {

using namespace Libraries::Np;
using namespace Libraries::Np::NpManager;

// --- NP Auth Hooks ---

bool HookAuthGetAuthorizationCode(s32 /*req_id*/, void* auth_code, s32* issuer_id, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    if (!IsAccountLoggedIn()) {
        if (out_res) *out_res = ORBIS_NP_ERROR_SIGNED_OUT;
        return true;
    }
    if (auth_code) {
        std::memset(auth_code, 0, 9);
        std::memcpy(auth_code, "AUTHCODE", 8);
    }
    if (issuer_id) *issuer_id = 100;
    if (out_res) *out_res = ORBIS_OK;
    return true;
}

bool HookAuthGetIdToken(s32 /*req_id*/, void* token, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    if (!IsAccountLoggedIn()) {
        if (out_res) *out_res = ORBIS_NP_ERROR_SIGNED_OUT;
        return true;
    }
    if (token) {
        std::memset(token, 0, 11);
        std::memcpy(token, "FAKE_TOKEN", 10);
    }
    if (out_res) *out_res = ORBIS_OK;
    return true;
}

// --- NP Manager Hooks ---

bool HookCheckNpAvailability(s32 /*req_id*/, OrbisNpOnlineId* /*online_id*/, s32* out_user_id) {
    if (!IsCustomHostActive()) return false;
    if (out_user_id) *out_user_id = 1; // RuntimeUserId
    return true;
}

bool HookCheckNpAvailabilityA(s32 /*req_id*/, Libraries::UserService::OrbisUserServiceUserId /*user_id*/, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    if (out_res) *out_res = IsAccountLoggedIn() ? ORBIS_OK : ORBIS_NP_ERROR_SIGNED_OUT;
    return true;
}

bool HookCheckNpReachability(s32 /*req_id*/, Libraries::UserService::OrbisUserServiceUserId /*user_id*/, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    if (out_res) *out_res = IsAccountLoggedIn() ? ORBIS_OK : ORBIS_NP_ERROR_SIGNED_OUT;
    return true;
}

bool HookCheckPlus(s32 /*req_id*/, const OrbisNpCheckPlusParameter* /*param*/,
                   OrbisNpCheckPlusResult* result, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    if (!IsAccountLoggedIn()) {
        if (out_res) *out_res = ORBIS_NP_ERROR_SIGNED_OUT;
        return true;
    }
    if (result) result->authorized = true;
    if (out_res) *out_res = ORBIS_OK;
    return true;
}

bool HookGetAccountId(OrbisNpOnlineId* online_id, u64* account_id, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    if (!IsAccountLoggedIn()) {
        if (account_id) *account_id = 0;
        if (out_res) *out_res = ORBIS_NP_ERROR_SIGNED_OUT;
        return true;
    }
    if (account_id) {
        *account_id = static_cast<u64>(std::hash<std::string>{}(online_id ? online_id->data : "")) |
                      0x8000000000000000ULL;
    }
    if (out_res) *out_res = ORBIS_OK;
    return true;
}

bool HookGetAccountIdA(Libraries::UserService::OrbisUserServiceUserId /*user_id*/,
                       u64* account_id, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    if (!IsAccountLoggedIn()) {
        if (account_id) *account_id = 0;
        if (out_res) *out_res = ORBIS_NP_ERROR_SIGNED_OUT;
        return true;
    }
    if (account_id) {
        *account_id = static_cast<u64>(std::hash<std::string>{}(GetOnlineId())) |
                      0x8000000000000000ULL;
    }
    if (out_res) *out_res = ORBIS_OK;
    return true;
}

bool HookGetNpId(Libraries::UserService::OrbisUserServiceUserId /*user_id*/,
                 OrbisNpId* np_id, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    if (!IsAccountLoggedIn()) {
        if (out_res) *out_res = ORBIS_NP_ERROR_SIGNED_OUT;
        return true;
    }
    if (np_id) {
        std::memset(np_id, 0, sizeof(*np_id));
        const std::string name = GetOnlineId();
        std::strncpy(np_id->handle.data, name.c_str(), sizeof(np_id->handle.data) - 1);
    }
    if (out_res) *out_res = ORBIS_OK;
    return true;
}

bool HookGetOnlineId(Libraries::UserService::OrbisUserServiceUserId /*user_id*/,
                     OrbisNpOnlineId* online_id, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    if (!IsAccountLoggedIn()) {
        if (out_res) *out_res = ORBIS_NP_ERROR_SIGNED_OUT;
        return true;
    }
    if (online_id) {
        std::memset(online_id->data, 0, sizeof(online_id->data));
        const std::string name = GetOnlineId();
        std::strncpy(online_id->data, name.c_str(), sizeof(online_id->data) - 1);
    }
    if (out_res) *out_res = ORBIS_OK;
    return true;
}

bool HookGetNpReachabilityState(Libraries::UserService::OrbisUserServiceUserId /*user_id*/,
                                OrbisNpReachabilityState* state, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    if (state) {
        *state = IsAccountLoggedIn() ? OrbisNpReachabilityState::Reachable
                                     : OrbisNpReachabilityState::Unavailable;
    }
    if (out_res) *out_res = ORBIS_OK;
    return true;
}

bool HookGetState(Libraries::UserService::OrbisUserServiceUserId /*user_id*/,
                  OrbisNpState* state, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    if (state) {
        *state = IsAccountLoggedIn() ? OrbisNpState::SignedIn : OrbisNpState::SignedOut;
    }
    if (out_res) *out_res = ORBIS_OK;
    return true;
}

bool HookHasSignedUp(Libraries::UserService::OrbisUserServiceUserId /*user_id*/,
                     bool* has_signed_up, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    if (has_signed_up) *has_signed_up = IsAccountLoggedIn();
    if (out_res) *out_res = ORBIS_OK;
    return true;
}

bool HookGetUserIdByAccountId(u64 /*account_id*/, Libraries::UserService::OrbisUserServiceUserId* user_id,
                              s32* out_res) {
    if (!IsCustomHostActive()) return false;
    if (!IsAccountLoggedIn()) {
        if (out_res) *out_res = ORBIS_NP_ERROR_SIGNED_OUT;
        return true;
    }
    if (user_id) *user_id = 1;
    if (out_res) *out_res = ORBIS_OK;
    return true;
}

bool HookGetUserIdByOnlineId(const OrbisNpOnlineId* /*online_id*/,
                             Libraries::UserService::OrbisUserServiceUserId* user_id, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    if (!IsAccountLoggedIn()) {
        if (out_res) *out_res = ORBIS_NP_ERROR_SIGNED_OUT;
        return true;
    }
    if (user_id) *user_id = 1;
    if (out_res) *out_res = ORBIS_OK;
    return true;
}

bool HookGetGamePresenceStatus(OrbisNpOnlineId* /*online_id*/, OrbisNpGamePresenseStatus* game_status, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    if (game_status) {
        *game_status = IsAccountLoggedIn() ? OrbisNpGamePresenseStatus::Online : OrbisNpGamePresenseStatus::Offline;
    }
    if (out_res) *out_res = ORBIS_OK;
    return true;
}

bool HookGetGamePresenceStatusA(Libraries::UserService::OrbisUserServiceUserId /*user_id*/,
                               OrbisNpGamePresenseStatus* game_status, s32* out_res) {
    if (!IsCustomHostActive()) return false;
    if (game_status) {
        *game_status = IsAccountLoggedIn() ? OrbisNpGamePresenseStatus::Online : OrbisNpGamePresenseStatus::Offline;
    }
    if (out_res) *out_res = ORBIS_OK;
    return true;
}

bool HookNotifyNpState(Libraries::UserService::OrbisUserServiceEventType event_type,
                       Libraries::UserService::OrbisUserServiceUserId /*user_id*/,
                       OrbisNpState* out_state) {
    if (!IsCustomHostActive()) return false;
    if (out_state) {
        *out_state = (event_type == Libraries::UserService::OrbisUserServiceEventType::Login && IsAccountLoggedIn())
                         ? OrbisNpState::SignedIn
                         : OrbisNpState::SignedOut;
    }
    return true;
}

bool ShouldQueueInitialSignedIn() {
    return IsCustomHostActive() && IsAccountLoggedIn();
}

bool HookWebApiSendRequest(const std::string& path, char* out_data, size_t max_data,
                           size_t* out_size, int* out_http_status) {
    if (!IsCustomHostActive()) return false;
    std::string_view body;
    if (path.find("friendList") != std::string::npos) {
        body = "{\"totalResults\":0,\"friendList\":[]}";
    } else if (path.find("blockList") != std::string::npos) {
        body = "{\"totalResults\":0,\"blockList\":[]}";
    } else {
        body = "{\"totalResults\":0}";
    }
    if (out_size) *out_size = body.size();
    if (out_http_status) *out_http_status = 200;
    if (out_data && max_data > 0) {
        const size_t copy_n = std::min(max_data, body.size());
        std::memcpy(out_data, body.data(), copy_n);
    }
    return true;
}

}  // namespace Extra::Hosts
