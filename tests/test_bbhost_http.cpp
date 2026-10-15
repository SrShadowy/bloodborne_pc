// SPDX-License-Identifier: GPL-2.0-or-later
// bbhost protocol transport: drives gpu/bbnet/bbhost/http_json.* against an
// in-process httplib server, so every request and reply stays on 127.0.0.1.
// The handlers echo what they observed back inside their JSON reply — the test
// reads the wire from the reply, sharing nothing with the server's thread.
// The pure classification cases (parse_reply, ResKind, field reads) need no
// server at all and run first.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <httplib.h>

#include "gpu/bbnet/bbhost/http_json.h"

namespace {

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
    assert(bbhost::server_base() == "https://thehuntersdream.com:18671");

    setenv("BB_HOST_SERVER", "192.168.1.7", 1);
    assert(bbhost::server_base() == "https://192.168.1.7:18671");  // port appended

    setenv("BB_HOST_SERVER", "192.168.1.7:9000", 1);
    assert(bbhost::server_base() == "https://192.168.1.7:9000");   // keeps its port

    setenv("BB_HOST_SCHEME", "http", 1);
    assert(bbhost::server_base() == "http://192.168.1.7:9000");

    setenv("BB_HOST_SCHEME", "gopher", 1);  // invalid: warns once, falls back to https
    assert(bbhost::server_base() == "https://192.168.1.7:9000");

    setenv("BB_HOST_SCHEME", "", 1);  // "" reads as unset on both variables
    setenv("BB_HOST_SERVER", "", 1);
    assert(bbhost::server_base() == "https://thehuntersdream.com:18671");
}

void TestFieldReads() {
    const nlohmann::json obj = {{"num", 9307}, {"str", "42"}, {"flt", 3.9},
                                {"yes", true}, {"bad", "abc"}};
    assert(bbhost::str_of(obj, "num") == "9307");
    assert(bbhost::str_of(obj, "str") == "42");
    assert(bbhost::str_of(obj, "flt") == "3");  // %lld truncation, as the reference
    assert(bbhost::str_of(obj, "yes", "dflt") == "dflt");
    assert(bbhost::str_of(obj, "absent", "dflt") == "dflt");
    assert(bbhost::str_of(nlohmann::json::array({1}), "num", "dflt") == "dflt");

    assert(bbhost::int_of(obj, "num") == 9307);
    assert(bbhost::int_of(obj, "str") == 42);
    assert(bbhost::int_of(obj, "flt") == 3);
    assert(bbhost::int_of(obj, "yes") == 1);
    assert(bbhost::int_of(obj, "absent", -1) == -1);
    // No digits at all keeps the default, where the reference's strtoll says 0.
    assert(bbhost::int_of(obj, "bad", -1) == -1);
}

void TestParseReply() {
    nlohmann::json out;
    std::string error;
    bbhost::HttpResult reply;

    reply.status = 500;
    reply.body = R"({"Message":"boom"})";
    assert(!bbhost::parse_reply(reply, out, error));
    assert(error == "HTTP 500");

    reply = {};
    reply.status = 200;
    reply.body = "not json{";
    assert(!bbhost::parse_reply(reply, out, error));
    assert(error.starts_with("bad JSON:"));

    reply = {};  // empty 2xx body: an empty reply, as the reference accepts
    reply.status = 200;
    assert(bbhost::parse_reply(reply, out, error) && out.is_object() && out.empty());

    reply.body = "[1,2]";
    assert(!bbhost::parse_reply(reply, out, error));
    assert(error == "the reply is not a JSON object");

    reply = {};
    reply.error = "Connection refused";
    assert(!bbhost::parse_reply(reply, out, error));
    assert(error == "Connection refused");

    assert(!bbhost::HttpResult{500, "", ""}.ok());
    assert(bbhost::HttpResult{200, "", ""}.ok());
}

void TestResKind() {
    std::string error;
    assert(bbhost::check_reskind(nlohmann::json{{"ResKind", 0}}, error) && error.empty());
    assert(!bbhost::check_reskind(nlohmann::json{{"ResKind", 1}, {"Message", "no room"}}, error));
    assert(error == "no room");
    assert(!bbhost::check_reskind(nlohmann::json{{"ResKind", 2}}, error));
    assert(error == "refused");  // absent Message falls back, never an empty error
    assert(!bbhost::check_reskind(nlohmann::json::object(), error));
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

    bbhost::HttpResult reply = bbhost::post_json("/echo", body, "tok-123");
    assert(reply.ok() && bbhost::parse_reply(reply, out, error));
    assert(bbhost::check_reskind(out, error));
    assert(out.value("HasToken", false));
    assert(bbhost::str_of(out, "Token") == "tok-123");
    assert(bbhost::str_of(out, "Ruleset") == "vanilla");
    assert(bbhost::str_of(out, "ContentType") == "application/json");
    assert(bbhost::str_of(out, "Accept") == "application/json");
    assert(bbhost::str_of(out, "Body") == body.dump());

    reply = bbhost::post_json("/echo", body, "");  // signed out: no token header
    assert(reply.ok() && bbhost::parse_reply(reply, out, error));
    assert(!out.value("HasToken", true));
    assert(bbhost::str_of(out, "Token").empty());

    reply = bbhost::post_json("/fail500", body, "tok");
    assert(!bbhost::parse_reply(reply, out, error) && error == "HTTP 500");

    reply = bbhost::post_json("/badjson", body, "tok");
    assert(!bbhost::parse_reply(reply, out, error));
    assert(error.starts_with("bad JSON:"));

    reply = bbhost::post_json("/empty", body, "tok");
    assert(bbhost::parse_reply(reply, out, error) && out.empty());

    reply = bbhost::post_json("/array", body, "tok");
    assert(!bbhost::parse_reply(reply, out, error));
    assert(error == "the reply is not a JSON object");

    reply = bbhost::post_json("/refuse", body, "tok");
    assert(bbhost::parse_reply(reply, out, error));
    assert(!bbhost::check_reskind(out, error) && error == "no room");

    srv.stop();
    serving.join();

    reply = bbhost::post_json("/echo", body, "tok");  // connection refused now
    assert(reply.status == 0 && !reply.error.empty());
    assert(!bbhost::parse_reply(reply, out, error) && error == reply.error);

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
    std::puts("test_bbhost_http: all tests passed");
    return 0;
}
