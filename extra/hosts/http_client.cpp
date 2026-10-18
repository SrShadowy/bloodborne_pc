// SPDX-License-Identifier: GPL-2.0-or-later
#include "http_client.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <httplib.h>
#include <string>

namespace Extra::Hosts {

std::string ServerBase() {
    const char* env_scheme = std::getenv("BB_HOST_SCHEME");
    std::string scheme = env_scheme ? env_scheme : "https";
    if (scheme.empty()) scheme = "https";
    if (scheme != "http" && scheme != "https") {
        std::fprintf(stderr, "Hosts: BB_HOST_SCHEME '%s' is not http or https, using https\n",
                     scheme.c_str());
        scheme = "https";
    }

    const char* env_server = std::getenv("BB_HOST_SERVER");
    std::string server = (env_server && env_server[0]) ? env_server : "thehuntersdream.com:18671";
    if (server.find(':') == std::string::npos) {
        server += ":18671";
    }
    return scheme + "://" + server;
}

HttpResult PostJson(const std::string& path, const nlohmann::json& body,
                    const std::string& token, int timeout_ms) {
    const std::string base = ServerBase();
    httplib::Client cli(base);
    cli.set_connection_timeout(std::chrono::milliseconds(timeout_ms));
    cli.set_read_timeout(std::chrono::milliseconds(timeout_ms));
    cli.set_write_timeout(std::chrono::milliseconds(timeout_ms));

    httplib::Headers headers = {
        {"Content-Type", "application/json"},
        {"Accept", "application/json"},
        {"X-BBHost-Ruleset", "vanilla"},
    };
    if (!token.empty()) {
        headers.emplace("X-BB-Token", token);
    }

    const std::string body_str = body.dump();
    const httplib::Result res = cli.Post(path, headers, body_str, "application/json");

    HttpResult out;
    if (!res) {
        out.status = 0;
        out.error = httplib::to_string(res.error());
        return out;
    }
    out.status = res->status;
    out.body = res->body;
    return out;
}

bool ParseReply(const HttpResult& res, nlohmann::json& out, std::string& error) {
    if (!res.Ok()) {
        if (res.status == 0) {
            error = res.error.empty() ? "network request failed" : res.error;
        } else {
            error = "HTTP " + std::to_string(res.status);
        }
        return false;
    }
    if (res.body.empty()) {
        out = nlohmann::json::object();
        return true;
    }
    try {
        out = nlohmann::json::parse(res.body);
    } catch (const nlohmann::json::parse_error& e) {
        error = std::string("bad JSON: ") + e.what();
        return false;
    }
    if (!out.is_object()) {
        error = "the reply is not a JSON object";
        return false;
    }
    return true;
}

bool CheckResKind(const nlohmann::json& reply, std::string& error) {
    if (!reply.is_object() || !reply.contains("ResKind")) {
        error = "the reply has no ResKind";
        return false;
    }
    if (IntOf(reply, "ResKind", -1) == 0) {
        error.clear();
        return true;
    }
    error = StrOf(reply, "Message");
    if (error.empty()) error = "refused";
    return false;
}

std::string StrOf(const nlohmann::json& obj, const std::string& key,
                  const std::string& dflt) {
    if (!obj.is_object()) return dflt;
    auto it = obj.find(key);
    if (it == obj.end()) return dflt;
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer() || it->is_number_unsigned()) {
        return std::to_string(it->get<long long>());
    }
    if (it->is_number_float()) {
        return std::to_string(static_cast<long long>(it->get<double>()));
    }
    return dflt;
}

long long IntOf(const nlohmann::json& obj, const std::string& key,
                long long dflt) {
    if (!obj.is_object()) return dflt;
    auto it = obj.find(key);
    if (it == obj.end()) return dflt;
    if (it->is_number_integer() || it->is_number_unsigned()) {
        return it->get<long long>();
    }
    if (it->is_number_float()) {
        return static_cast<long long>(it->get<double>());
    }
    if (it->is_boolean()) {
        return it->get<bool>() ? 1 : 0;
    }
    if (it->is_string()) {
        const std::string text = it->get<std::string>();
        char* end = nullptr;
        const long long val = std::strtoll(text.c_str(), &end, 10);
        return end == text.c_str() ? dflt : val;
    }
    return dflt;
}

}  // namespace Extra::Hosts
