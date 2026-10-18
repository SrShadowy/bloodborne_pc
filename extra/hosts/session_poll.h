// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <functional>
#include <nlohmann/json.hpp>

namespace Extra::Hosts {

using EventHandler = std::function<void(const nlohmann::json& event)>;

// Starts the event poller background thread querying /np/events/poll.
void PollerStart(EventHandler handler);

// Stops the event poller thread.
void PollerStop();

// Returns whether the poller thread is currently active.
bool PollerIsRunning();

}  // namespace Extra::Hosts
