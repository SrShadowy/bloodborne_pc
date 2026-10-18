// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

namespace Extra::Hosts {

// Returns the online ID configured for this session (from BB_HOST_ONLINE_ID,
// ~/.config/bbport-launcher/account.json, or "Player" as fallback).
std::string GetOnlineId();

// Returns the account token (from BB_HOST_TOKEN or the launcher's account.json).
// Empty if unauthenticated.
std::string GetAccountToken();

// True if an account token is present.
bool IsAccountLoggedIn();

// True if the server rejected the token with HTTP 401.
bool IsAccountRefused();
void SetAccountRefused(bool refused);

// For testing / overrides: set explicit online_id and token in memory.
void SetAccountOverride(const std::string& online_id, const std::string& token);
void ClearAccountOverride();

}  // namespace Extra::Hosts
