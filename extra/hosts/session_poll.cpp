// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_poll.h"
#include "account.h"
#include "http_client.h"
#include "session_room.h"
#include "session_state.h"

#include <pthread.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace Extra::Hosts {
namespace {

std::atomic<bool> g_poll_run{false};
std::mutex g_poll_mu;
std::condition_variable g_poll_cv;
std::thread g_poll_thread;
EventHandler g_event_handler;
long long g_cursor = 0;

void PollerMain() {
    pthread_setname_np(pthread_self(), "bb-np-poll");
    int interval_ms = 250;
    auto last_heartbeat = std::chrono::steady_clock::now();
    auto last_signaling = std::chrono::steady_clock::now();

    while (g_poll_run.load(std::memory_order_relaxed)) {
        const auto now = std::chrono::steady_clock::now();
        if (InRoom() && now - last_heartbeat >= std::chrono::seconds(5)) {
            last_heartbeat = now;
            const int hb = ServerHeartbeat(CurrentSessionId(), CurrentMemberId());
            if (hb == 0) {
                // Server removed us from room
                RoomClear();
            }
        }
        if (InRoom() && now - last_signaling >= std::chrono::seconds(15)) {
            last_signaling = now;
            std::string err;
            (void)ServerSignalingUpdate(err);
        }

        const std::string me = GetOnlineId();
        nlohmann::json body = {
            {"OnlineId", me},
            {"Cursor", g_cursor},
            {"Categories", nlohmann::json::array({"matching2"})},
        };

        const HttpResult res = PostJson("/np/events/poll", body, GetAccountToken(), 4000);
        nlohmann::json reply;
        std::string err;

        if (res.Ok() && ParseReply(res, reply, err)) {
            interval_ms = 250;
            const long long next = IntOf(reply, "NextCursor", g_cursor);
            if (IntOf(reply, "HasEvent", 0) != 0) {
                EventHandler handler;
                {
                    std::lock_guard<std::mutex> lock(g_poll_mu);
                    handler = g_event_handler;
                }
                if (handler) {
                    handler(reply);
                }
                nlohmann::json ack_body = {
                    {"OnlineId", me},
                    {"Cursor", next},
                };
                (void)PostJson("/np/events/ack", ack_body, GetAccountToken(), 4000);
                g_cursor = next;
                continue;  // Drain immediately without waiting
            }
            g_cursor = next > g_cursor ? next : g_cursor;
        } else if (res.status == 401 || IsAccountRefused()) {
            SetAccountRefused(true);
            interval_ms = 60000;  // Back off 1 minute on refused account
        } else {
            interval_ms = (interval_ms < 4000) ? interval_ms * 2 : 4000;
        }

        std::unique_lock<std::mutex> lock(g_poll_mu);
        g_poll_cv.wait_for(lock, std::chrono::milliseconds(interval_ms), [] {
            return !g_poll_run.load(std::memory_order_relaxed);
        });
    }
}

}  // namespace

void PollerStart(EventHandler handler) {
    std::lock_guard<std::mutex> lock(g_poll_mu);
    g_event_handler = std::move(handler);
    if (g_poll_run.load(std::memory_order_relaxed)) return;
    g_poll_run.store(true, std::memory_order_relaxed);
    g_cursor = 0;
    g_poll_thread = std::thread(PollerMain);
}

void PollerStop() {
    {
        std::lock_guard<std::mutex> lock(g_poll_mu);
        if (!g_poll_run.load(std::memory_order_relaxed)) return;
        g_poll_run.store(false, std::memory_order_relaxed);
        g_poll_cv.notify_all();
    }
    if (g_poll_thread.joinable()) {
        g_poll_thread.join();
    }
}

bool PollerIsRunning() {
    return g_poll_run.load(std::memory_order_relaxed);
}

}  // namespace Extra::Hosts
