#pragma once

#include "server_state.h"
#include "player_runtime.h"
#include "server_tick.h"
#include "server_constants.h"
#include "server_util.h"
#include "server_spawning.h"

// ---- Derived player combat stats ----

// Computes effective offensive power from melee skill + equipment.
[[nodiscard]] int computePlayerOffence(const PlayerRuntime& p, const ServerState& state);

// Computes effective defence from shielding skill.
[[nodiscard]] int computePlayerDefence(const PlayerRuntime& p, const ServerState& state);

// Computes effective armor value from equipment defense bonuses.
[[nodiscard]] int computePlayerArmor(const PlayerRuntime& p, const ServerState& state);

// Computes effective evasion from evasion skill + equipment.
[[nodiscard]] int computePlayerEvasion(const PlayerRuntime& p, const ServerState& state);

void combat(ServerState& state, PlayerRuntime p, TickResult& result);
