#pragma once

#include "server_state.h"
#include "player_runtime.h"
#include "server_tick.h"
#include "server_constants.h"
#include "server_util.h"
#include "server_spawning.h"

void combat(ServerState& state, PlayerRuntime p, TickResult& result);
