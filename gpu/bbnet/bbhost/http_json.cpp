// SPDX-License-Identifier: GPL-2.0-or-later
// Transport half of the bbhost protocol (http_json.h): where the server is, one
// POST with the protocol's headers, and how to classify what comes back. The
// callers (account, session) own the endpoints, the retries and the logging —
// this file only moves bytes and reads the reply, so it never touches guest
// state and is safe from any thread.
#include "http_json.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <httplib.h>

namespace bbhost {
namespace {

// getenv() with "" read as unset, the way the reference reads its configuration.
std::string EnvOr(const char* name, const char* dflt) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : dflt;
}

// No gameplay plugins are ported (the engine hooks stay out of this round), so
// every session is vanilla — what the reference client sends for its own.
constexpr const char* kRuleset = "vanilla";
constexpr int kDefaultTimeoutMs = 5000;

}  // namespace

std::string server_base() {
    std::string scheme = EnvOr("BB_HOST_SCHEME", "https");
    if (scheme != "http" && scheme != "https") {
        // A typo must not send JSON into a bogus scheme: say it once, keep https.
        static std::atomic<bool> warned{false};
        if (!warned.exchange(true)) {
            std::printf("Online: BB_HOST_SCHEME '%s' is not http or https, using https\n",
                        scheme.c_str());
        }
        scheme = "https";
    }
    std::string host = EnvOr("BB_HOST_SERVER", "thehuntersdream.com");
    // BB_HOST_SERVER may carry its own port ("192.168.0.2:9000"); otherwise the
    // protocol's. IPv4 literals only — a ':' decides, as everywhere in bbhost.
    if (host.find(':') == std::string::npos) host += ":18671";
    return scheme + "://" + host;
}

HttpResult post_json(const std::string& path, const nlohmann::json& body,
                     const std::string& token, int timeout_ms) {
    HttpResult result;
    if (timeout_ms <= 0) timeout_ms = kDefaultTimeoutMs;  // keep every call bounded

    // A fresh client per call: the reference opens its own handle per request
    // too, so no connection outlives a caller and none is shared between
    // threads. Timeout on all three phases bounds the whole exchange.
    httplib::Client cli(server_base());
    const auto timeout = std::chrono::milliseconds(timeout_ms);
    cli.set_connection_timeout(timeout);
    cli.set_read_timeout(timeout);
    cli.set_write_timeout(timeout);
    // The reference does not follow redirects either: the protocol answers in
    // place, and a redirect would carry the token somewhere it should not go.
    cli.set_follow_location(false);

    httplib::Headers headers = {
        {"Accept", "application/json"},
        {"X-BBHost-Ruleset", kRuleset},
    };
    if (!token.empty()) headers.emplace("X-BB-Token", token);
    // Content-Type is set by Post's content_type argument (it replaces rather
    // than duplicates), so it is not in the map above.
    auto response = cli.Post(path, headers, body.dump(), "application/json");
    if (!response) {
        result.error = httplib::to_string(response.error());
        return result;
    }
    result.status = response->status;
    result.body = response->body;
    return result;
}

bool parse_reply(const HttpResult& reply, nlohmann::json& out, std::string& error) {
    out = nlohmann::json::object();
    error.clear();
    if (!reply.error.empty()) {
        error = reply.error;
        return false;
    }
    if (reply.status < 200 || reply.status >= 300) {
        // The status carries the meaning an account caller cares about (401 is
        // refused); the body of an error reply is not the protocol's JSON.
        error = "HTTP " + std::to_string(reply.status);
        return false;
    }
    if (reply.body.empty()) return true;  // an empty 2xx body is an empty reply
    try {
        out = nlohmann::json::parse(reply.body);
    } catch (const nlohmann::json::parse_error& e) {
        out = nlohmann::json::object();
        error = "bad JSON: ";
        error += e.what();
        return false;
    }
    if (!out.is_object()) {
        error = "the reply is not a JSON object";
        out = nlohmann::json::object();
        return false;
    }
    return true;
}

bool check_reskind(const nlohmann::json& reply, std::string& error) {
    if (!reply.is_object() || !reply.contains("ResKind")) {
        // Strict, as every account and room call in the reference: a reply
        // without the envelope is not a reply to that call. (The event poll
        // tolerates its absence and checks ResKind itself.)
        error = "the reply has no ResKind";
        return false;
    }
    if (int_of(reply, "ResKind", -1) == 0) {
        error.clear();
        return true;
    }
    error = str_of(reply, "Message");
    if (error.empty()) error = "refused";
    return false;
}

std::string str_of(const nlohmann::json& obj, const char* key, std::string dflt) {
    if (!obj.is_object()) return dflt;
    const auto it = obj.find(key);
    if (it == obj.end()) return dflt;
    if (it->is_string()) return it->get<std::string>();
    // Numbers print in the reference's %lld form: get<long long> covers signed,
    // unsigned and float alike, truncating the float exactly as %lld would.
    if (it->is_number()) return std::to_string(it->get<long long>());
    return dflt;  // booleans and nested values have no string form the wire uses
}

long long int_of(const nlohmann::json& obj, const char* key, long long dflt) {
    if (!obj.is_object()) return dflt;
    const auto it = obj.find(key);
    if (it == obj.end()) return dflt;
    // Numbers read as numbers: get<long long> truncates a float, matching the
    // reference's %lld of the same value.
    if (it->is_number()) return it->get<long long>();
    if (it->is_boolean()) return it->get<bool>() ? 1 : 0;
    // The wire sends ports and ids as strings on some calls. A string with no
    // digits at all keeps `dflt` — the reference's bare strtoll reads 0 there,
    // which could pass a malformed value off as success or as a valid field.
    if (it->is_string()) {
        const std::string& text = it->get_ref<const std::string&>();
        char* end = nullptr;
        const long long value = std::strtoll(text.c_str(), &end, 10);
        return end == text.c_str() ? dflt : value;
    }
    return dflt;
}

}  // namespace bbhost
