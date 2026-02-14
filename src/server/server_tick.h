// Fixed-rate game tick: combat resolution, AI movement, respawn management.
#pragma once

#include "server_state.h"

#include <string>

struct TickResult {
    bool state_changed = false;
    std::string event_text;
};

// Advances the server simulation by one tick (typically 500 ms).
// Returns whether anything changed and a human-readable event summary.
[[nodiscard]] TickResult advanceServerTick(ServerState& state, int tick_ms);
