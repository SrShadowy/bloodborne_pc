// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace Extra::Hosts {

struct HttpResult {
    int status = 0;
    std::string body;
    std::string error;

    bool Ok() const { return status >= 200 && status < 300; }
};

std::string ServerBase();

HttpResult PostJson(const std::string& path, const nlohmann::json& body,
                    const std::string& token = "", int timeout_ms = 5000);

bool ParseReply(const HttpResult& res, nlohmann::json& out, std::string& error);

bool CheckResKind(const nlohmann::json& reply, std::string& error);

std::string StrOf(const nlohmann::json& obj, const std::string& key,
                  const std::string& dflt = "");

long long IntOf(const nlohmann::json& obj, const std::string& key,
                long long dflt = 0);

}  // namespace Extra::Hosts
