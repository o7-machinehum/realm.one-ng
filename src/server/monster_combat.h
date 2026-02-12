#pragma once

#include "server_runtime.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct MonsterCombatResult {
    bool changed = false;
    std::string event_text;
    std::vector<ENetPeer*> defeated_players;
};

// Applies monster melee attacks for one server combat tick.
MonsterCombatResult resolveMonsterAttacks(
    std::unordered_map<ENetPeer*, PlayerRuntime>& players,
    std::vector<MonsterRuntime>& monsters,
    const std::function<bool(const std::string&, int&, int&)>& find_respawn_tile,
    int melee_range_tiles
);
