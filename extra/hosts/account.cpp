// SPDX-License-Identifier: GPL-2.0-or-later
#include "account.h"

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>

namespace Extra::Hosts {
namespace {

std::mutex g_account_mu;
bool g_has_override = false;
std::string g_override_online_id;
std::string g_override_token;
bool g_account_refused = false;

struct ConfigData {
    std::string online_id;
    std::string token;
    bool loaded = false;
};

ConfigData LoadConfig() {
    ConfigData d;
    if (const char* env_tok = std::getenv("BB_HOST_TOKEN")) {
        d.token = env_tok;
    } else if (const char* legacy_tok = std::getenv("BB_HD_TOKEN")) {
        d.token = legacy_tok;
    }

    if (const char* env_id = std::getenv("BB_HOST_ONLINE_ID")) {
        d.online_id = env_id;
    } else if (const char* legacy_id = std::getenv("BB_HD_ONLINE_ID")) {
        d.online_id = legacy_id;
    }

    if (!d.token.empty() && !d.online_id.empty()) {
        d.loaded = true;
        return d;
    }

    const char* home = std::getenv("HOME");
    if (!home) return d;

    const std::string path = std::string(home) + "/.config/bbport-launcher/account.json";
    std::ifstream f(path);
    if (!f.is_open()) return d;

    try {
        nlohmann::json j;
        f >> j;
        if (d.online_id.empty()) {
            d.online_id = j.value("OnlineId", j.value("online_id", ""));
        }
        if (d.token.empty()) {
            d.token = j.value("Token", j.value("token", ""));
        }
    } catch (...) {
        // Fallback to defaults
    }
    d.loaded = true;
    return d;
}

}  // namespace

std::string GetOnlineId() {
    std::lock_guard<std::mutex> lock(g_account_mu);
    if (g_has_override) return g_override_online_id;
    const ConfigData cfg = LoadConfig();
    return cfg.online_id.empty() ? "Player" : cfg.online_id;
}

std::string GetAccountToken() {
    std::lock_guard<std::mutex> lock(g_account_mu);
    if (g_has_override) return g_override_token;
    return LoadConfig().token;
}

bool IsAccountLoggedIn() {
    return !GetAccountToken().empty();
}

bool IsAccountRefused() {
    std::lock_guard<std::mutex> lock(g_account_mu);
    return g_account_refused;
}

void SetAccountRefused(bool refused) {
    std::lock_guard<std::mutex> lock(g_account_mu);
    g_account_refused = refused;
}

void SetAccountOverride(const std::string& online_id, const std::string& token) {
    std::lock_guard<std::mutex> lock(g_account_mu);
    g_override_online_id = online_id;
    g_override_token = token;
    g_has_override = true;
    g_account_refused = false;
}

void ClearAccountOverride() {
    std::lock_guard<std::mutex> lock(g_account_mu);
    g_has_override = false;
    g_override_online_id.clear();
    g_override_token.clear();
    g_account_refused = false;
}

}  // namespace Extra::Hosts
