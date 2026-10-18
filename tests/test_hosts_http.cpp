// SPDX-License-Identifier: GPL-2.0-or-later
// hosts protocol transport: drives extra/hosts/http_client.* against an
// in-process httplib server, so every request and reply stays on 127.0.0.1.
// The handlers echo what they observed back inside their JSON reply — the test
// reads the wire from the reply, sharing nothing with the server's thread.
// The pure classification cases (ParseReply, ResKind, field reads) need no
// server at all and run first.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <httplib.h>

#include "hosts/http_client.h"

namespace {

using namespace Extra::Hosts;

// Answer 200/ResKind 0 with this request's headers and body, so the test can
// assert on exactly what went over the wire.
void Echo(const httplib::Request& req, httplib::Response& res) {
    const nlohmann::json reply = {
        {"ResKind", 0},
        {"HasToken", req.has_header("X-BB-Token")},
        {"Token", req.get_header_value("X-BB-Token")},
        {"Ruleset", req.get_header_value("X-BBHost-Ruleset")},
        {"ContentType", req.get_header_value("Content-Type")},
        {"Accept", req.get_header_value("Accept")},
        {"Body", req.body},
    };
    res.set_content(reply.dump(), "application/json");
}

void TestServerBase() {
    unsetenv("BB_HOST_SCHEME");
    unsetenv("BB_HOST_SERVER");
    assert(ServerBase() == "https://thehuntersdream.com:18671");

    setenv("BB_HOST_SERVER", "192.168.1.7", 1);
    assert(ServerBase() == "https://192.168.1.7:18671");  // port appended

    setenv("BB_HOST_SERVER", "192.168.1.7:9000", 1);
    assert(ServerBase() == "https://192.168.1.7:9000");   // keeps its port

    setenv("BB_HOST_SCHEME", "http", 1);
    assert(ServerBase() == "http://192.168.1.7:9000");

    setenv("BB_HOST_SCHEME", "gopher", 1);  // invalid: warns once, falls back to https
    assert(ServerBase() == "https://192.168.1.7:9000");

    setenv("BB_HOST_SCHEME", "", 1);  // "" reads as unset on both variables
    setenv("BB_HOST_SERVER", "", 1);
    assert(ServerBase() == "https://thehuntersdream.com:18671");
}

void TestFieldReads() {
    const nlohmann::json obj = {{"num", 9307}, {"str", "42"}, {"flt", 3.9},
                                {"yes", true}, {"bad", "abc"}};
    assert(StrOf(obj, "num") == "9307");
    assert(StrOf(obj, "str") == "42");
    assert(StrOf(obj, "flt") == "3");  // %lld truncation, as the reference
    assert(StrOf(obj, "yes", "dflt") == "dflt");
    assert(StrOf(obj, "absent", "dflt") == "dflt");
    assert(StrOf(nlohmann::json::array({1}), "num", "dflt") == "dflt");

    assert(IntOf(obj, "num") == 9307);
    assert(IntOf(obj, "str") == 42);
    assert(IntOf(obj, "flt") == 3);
    assert(IntOf(obj, "yes") == 1);
    assert(IntOf(obj, "absent", -1) == -1);
    // No digits at all keeps the default, where the reference's strtoll says 0.
    assert(IntOf(obj, "bad", -1) == -1);
}

void TestParseReply() {
    nlohmann::json out;
    std::string error;
    HttpResult reply;

    reply.status = 500;
    reply.body = R"({"Message":"boom"})";
    assert(!ParseReply(reply, out, error));
    assert(error == "HTTP 500");

    reply = {};
    reply.status = 200;
    reply.body = "not json{";
    assert(!ParseReply(reply, out, error));
    assert(error.starts_with("bad JSON:"));

    reply = {};  // empty 2xx body: an empty reply, as the reference accepts
    reply.status = 200;
    assert(ParseReply(reply, out, error) && out.is_object() && out.empty());

    reply.body = "[1,2]";
    assert(!ParseReply(reply, out, error));
    assert(error == "the reply is not a JSON object");

    reply = {};
    reply.error = "Connection refused";
    assert(!ParseReply(reply, out, error));
    assert(error == "Connection refused");

    assert(!HttpResult{500, "", ""}.Ok());
    assert(HttpResult{200, "", ""}.Ok());
}

void TestResKind() {
    std::string error;
    assert(CheckResKind(nlohmann::json{{"ResKind", 0}}, error) && error.empty());
    assert(!CheckResKind(nlohmann::json{{"ResKind", 1}, {"Message", "no room"}}, error));
    assert(error == "no room");
    assert(!CheckResKind(nlohmann::json{{"ResKind", 2}}, error));
    assert(error == "refused");  // absent Message falls back, never an empty error
    assert(!CheckResKind(nlohmann::json::object(), error));
    assert(error == "the reply has no ResKind");
}

// One full pass over the wire: protocol headers out, reply classification back,
// and the transport failure once nothing listens any more.
void TestRoundtrip() {
    httplib::Server srv;
    srv.Post("/echo", Echo);
    srv.Post("/fail500", [](const httplib::Request&, httplib::Response& res) {
        res.status = 500;
        res.set_content(R"({"Message":"server exploded"})", "application/json");
    });
    srv.Post("/badjson", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("not json{", "application/json");
    });
    srv.Post("/empty", [](const httplib::Request&, httplib::Response&) {});  // 200, no body
    srv.Post("/array", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("[1,2]", "application/json");
    });
    srv.Post("/refuse", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"ResKind":1,"Message":"no room"})", "application/json");
    });

    const int port = srv.bind_to_any_port("127.0.0.1");
    assert(port > 0);
    std::thread serving([&srv] { srv.listen_after_bind(); });
    srv.wait_until_ready();

    setenv("BB_HOST_SCHEME", "http", 1);
    const std::string server = "127.0.0.1:" + std::to_string(port);
    setenv("BB_HOST_SERVER", server.c_str(), 1);

    const nlohmann::json body = {{"OnlineId", "hunter"}, {"RoomId", 7}};
    nlohmann::json out;
    std::string error;

    HttpResult reply = PostJson("/echo", body, "tok-123");
    assert(reply.Ok() && ParseReply(reply, out, error));
    assert(CheckResKind(out, error));
    assert(out.value("HasToken", false));
    assert(StrOf(out, "Token") == "tok-123");
    assert(StrOf(out, "Ruleset") == "vanilla");
    assert(StrOf(out, "ContentType") == "application/json");
    assert(StrOf(out, "Accept") == "application/json");
    assert(StrOf(out, "Body") == body.dump());

    reply = PostJson("/echo", body, "");  // signed out: no token header
    assert(reply.Ok() && ParseReply(reply, out, error));
    assert(!out.value("HasToken", true));
    assert(StrOf(out, "Token").empty());

    reply = PostJson("/fail500", body, "tok");
    assert(!ParseReply(reply, out, error) && error == "HTTP 500");

    reply = PostJson("/badjson", body, "tok");
    assert(!ParseReply(reply, out, error));
    assert(error.starts_with("bad JSON:"));

    reply = PostJson("/empty", body, "tok");
    assert(ParseReply(reply, out, error) && out.empty());

    reply = PostJson("/array", body, "tok");
    assert(!ParseReply(reply, out, error));
    assert(error == "the reply is not a JSON object");

    reply = PostJson("/refuse", body, "tok");
    assert(ParseReply(reply, out, error));
    assert(!CheckResKind(out, error) && error == "no room");

    srv.stop();
    serving.join();

    reply = PostJson("/echo", body, "tok");  // connection refused now
    assert(reply.status == 0 && !reply.error.empty());
    assert(!ParseReply(reply, out, error) && error == reply.error);

    unsetenv("BB_HOST_SCHEME");
    unsetenv("BB_HOST_SERVER");
}

}  // namespace

int main() {
    TestServerBase();
    TestFieldReads();
    TestParseReply();
    TestResKind();
    TestRoundtrip();
    std::puts("test_hosts_http: all tests passed");
    return 0;
}
