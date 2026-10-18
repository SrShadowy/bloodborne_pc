// SPDX-License-Identifier: GPL-2.0-or-later
#include "backend.h"
#include "backend_hd.h"

#include <cstdlib>
#include <string>

namespace Extra::Hosts {

bool IsCustomHostActive() {
    const char* env = std::getenv("BB_ONLINE_BACKEND");
    if (!env || !env[0]) return false;
    const std::string s(env);
    return (s == "hd" || s == "thehuntersdream" || s == "bbhost" || s == "hosts");
}

OnlineBackend& GetActiveBackend() {
    static HdBackend s_hd_backend;
    return s_hd_backend;
}

}  // namespace Extra::Hosts
