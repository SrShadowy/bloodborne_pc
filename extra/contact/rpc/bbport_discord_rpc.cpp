// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the Discord Rich Presence worker -- a background thread that connects to the Discord
// client, re-sends the presence every few seconds and clears it when the feature is turned off.
// The IPC transport is bbport_discord_rpc_ipc.cpp; the card's text is bbport_discord_rpc_activity.cpp.
#include "bbport_discord_rpc.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "bbport_discord_rpc_internal.h"
#include "bbport_settings.h"
#include "bbport_threads.h"
#include "common/thread.h"

namespace BbDiscordRpc {
namespace {

using detail::IpcConnection;

/// How long the worker sleeps with the feature off, and between updates while connected.
constexpr auto kDisabledPoll = std::chrono::seconds(2);
/// How long it waits before retrying a Discord client that is not running.
constexpr auto kReconnectDelay = std::chrono::seconds(5);
/// Discord rate-limits activity updates; five seconds is what the client itself sends.
constexpr auto kUpdateInterval = std::chrono::seconds(5);

/// The opcode that carries SET_ACTIVITY in both directions.
constexpr std::uint32_t kOpFrame = 1;

std::thread g_worker_thread;
std::mutex g_mutex;
std::condition_variable g_cv;
std::atomic<bool> g_stop_flag{false};
std::atomic<bool> g_connected{false};
std::atomic<bool> g_running{false};

/// Waits up to `delay`, waking early on shutdown or a setting the predicate names.
template <typename Predicate>
void WaitFor(std::chrono::milliseconds delay, Predicate predicate) {
    std::unique_lock lock(g_mutex);
    g_cv.wait_for(lock, delay, [&] { return g_stop_flag.load() || predicate(); });
}

/// Clears the presence and drops the connection.
void Disconnect(IpcConnection& connection) {
    if (!connection.IsOpen()) {
        return;
    }
    detail::Send(connection, kOpFrame, detail::ClearActivityJson(detail::CurrentProcessId()));
    connection.Close();
    g_connected = false;
}

/// Reads and discards a reply, so an unread one cannot stall the next send.
void DrainReplies(IpcConnection& connection) {
    while (detail::HasPendingFrame(connection)) {
        std::uint32_t opcode = 0;
        std::string reply;
        if (!detail::Read(connection, opcode, reply)) {
            return;
        }
    }
}

void WorkerLoop() {
    IpcConnection connection;

    while (!g_stop_flag.load()) {
        const auto& settings = BbSettings::Get();
        if (!settings.discord_rpc.load()) {
            Disconnect(connection);
            WaitFor(kDisabledPoll, [] { return BbSettings::Get().discord_rpc.load(); });
            continue;
        }

        if (!g_connected.load()) {
            connection = detail::Connect();
            if (!connection.IsOpen() || !detail::PerformHandshake(connection)) {
                connection.Close();
                WaitFor(kReconnectDelay, [] { return false; });
                continue;
            }
            g_connected = true;
        }

        const int lang = settings.menu_language.load();
        if (!detail::Send(connection, kOpFrame, detail::ActivityJson(lang))) {
            connection.Close();
            g_connected = false;
            WaitFor(kReconnectDelay, [] { return false; });
            continue;
        }
        DrainReplies(connection);

        WaitFor(kUpdateInterval, [] { return !BbSettings::Get().discord_rpc.load(); });
    }

    Disconnect(connection);
}

} // namespace

void Init() {
    if (g_running.exchange(true)) {
        return;
    }
    g_stop_flag = false;
    g_worker_thread = std::thread([] {
        Common::SetCurrentThreadName("bb:discord_rpc");
        BbThreads::MakeBackground();
        WorkerLoop();
    });
}

void Shutdown() {
    if (!g_running.exchange(false)) {
        return;
    }
    g_stop_flag = true;
    g_cv.notify_all();
    if (g_worker_thread.joinable()) {
        g_worker_thread.join();
    }
}

bool IsConnected() {
    return g_connected.load();
}

const char* GetStatusText(int lang) {
    const auto pick = [lang](const char* en, const char* pt, const char* ru) {
        if (lang == BbSettings::LangPortuguese) {
            return pt;
        }
        if (lang == BbSettings::LangRussian) {
            return ru;
        }
        return en;
    };
    if (!BbSettings::Get().discord_rpc.load()) {
        return pick("Disabled", "Desativado", "Отключено");
    }
    if (g_connected.load()) {
        return pick("● Connected to Discord", "● Conectado ao Discord", "● Подключено к Discord");
    }
    return pick("○ Searching for Discord...", "○ Procurando Discord...", "○ Поиск Discord...");
}

ActivityDisplay GetCurrentActivityDisplay(int lang) {
    return detail::FormatActivity(lang);
}

} // namespace BbDiscordRpc
