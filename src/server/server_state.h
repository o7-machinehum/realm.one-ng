// Aggregate of all mutable server game state, replacing the dozens of
// local variables and closure-captured lambdas that lived in recvLoop().
#pragma once

#include "server_runtime.h"
#include "global_settings.h"
#include "item_defs.h"
#include "monster_defs.h"
#include "npc_defs.h"

#include <enet/enet.h>

#include <string>
#include <unordered_map>
#include <vector>

class World;
class AuthDb;

struct MonsterRespawnEntry {
    MonsterRuntime mon;
    int remaining_ms = 0;
};

struct ServerState {
    // ---- Entity collections ----
    std::unordered_map<ENetPeer*, PlayerRuntime> players;
    std::vector<MonsterRuntime> monsters;
    std::vector<NpcRuntime> npcs;
    std::vector<GroundItemRuntime> items;
    std::vector<MonsterRespawnEntry> pending_respawns;

    // ---- ID counters ----
    int next_monster_id = 1;
    int next_npc_id = 1;
    int next_item_id = 1;

    // ---- Loaded definitions (immutable after init) ----
    std::vector<MonsterDef> monster_defs_storage;
    std::unordered_map<std::string, const MonsterDef*> monster_defs_by_id;

    std::vector<NpcDef> npc_defs_storage;
    std::unordered_map<std::string, const NpcDef*> npc_defs_by_id;

    std::vector<ItemDef> item_defs_storage;
    std::unordered_map<std::string, const ItemDef*> item_defs_by_id;

    GlobalSettings settings;

    // ---- External references (not owned) ----
    World* world = nullptr;
    AuthDb* auth_db = nullptr;
    ENetHost* host = nullptr;
};
