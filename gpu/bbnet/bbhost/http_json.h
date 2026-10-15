// SPDX-License-Identifier: GPL-2.0-or-later
// The host's own HTTP+JSON traffic to the Hunter's Dream server (the bbhost
// protocol): the account and session services, always POST + JSON with the
// account's token (X-BB-Token) and the session's ruleset (X-BBHost-Ruleset:
// "vanilla" without gameplay plugins). Transport and reply classification
// only — no protocol knowledge beyond the ResKind envelope every reply
// carries; the callers (account, session) own their endpoints and state.
// Synchronous, callable from any thread, never touches guest state.
#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace bbhost {

// One HTTP exchange: the status, the body as text, and the transport error
// (empty on success). status is 0 when the transport failed.
struct HttpResult {
    long status = 0;
    std::string body;
    std::string error;
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

// The public server's origin: "<BB_HOST_SCHEME>://<BB_HOST_SERVER>:18671"
// (https://thehuntersdream.com:18671 when unset; BB_HOST_SERVER may carry its
// own port, e.g. "192.168.0.2:18671"; any other BB_HOST_SCHEME falls back to
// https). Certificates are always verified against the system store.
std::string server_base();

// POST `body` as JSON to "<server_base()><path>", with the protocol's headers:
// Accept/Content-Type JSON, X-BBHost-Ruleset vanilla, and X-BB-Token when
// `token` is not empty (the header every signed-in account's calls carry).
HttpResult post_json(const std::string& path, const nlohmann::json& body,
                     const std::string& token, int timeout_ms = 5000);

// The reply as a JSON object: false fills `error` with why not — the transport
// error, an HTTP status outside 2xx, or a body that is not a JSON object.
// An empty 2xx body counts as {} (the reference client accepts it too).
bool parse_reply(const HttpResult& reply, nlohmann::json& out, std::string& error);

// The envelope the account and room replies carry: ResKind 0 is success,
// otherwise Message says why not (strict — a reply without ResKind fails;
// the event poll tolerates its absence, so it checks ResKind itself).
bool check_reskind(const nlohmann::json& reply, std::string& error);

// Field reads that never throw: `dflt` when the member is absent or of an
// unexpected type, converting the types the wire uses for each other (the
// server sends ports and ids as numbers or strings depending on the call).
// int_of keeps `dflt` for a string with no digits at all, where the reference
// client's bare strtoll would return 0 (fail-closed: a malformed field can
// never read as a valid zero).
std::string str_of(const nlohmann::json& obj, const char* key, std::string dflt = "");
long long int_of(const nlohmann::json& obj, const char* key, long long dflt = 0);

}  // namespace bbhost
