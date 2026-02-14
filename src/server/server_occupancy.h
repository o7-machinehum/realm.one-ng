// Tile occupancy checks and spatial queries used by movement, spawning, and combat.
#pragma once

#include "server_state.h"

#include <string>

// ---- Tile occupancy ----
[[nodiscard]] bool isTileOccupiedByMonster(const ServerState& state, const std::string& room, int x, int y);
[[nodiscard]] bool isTileOccupiedByNpc(const ServerState& state, const std::string& room, int x, int y);
[[nodiscard]] bool isTileOccupiedByPlayer(const ServerState& state, ENetPeer* exclude,
                                          const std::string& room, int x, int y);

// ---- Movement validation ----
[[nodiscard]] bool canMonsterOccupyTile(const ServerState& state, const MonsterRuntime& mon,
                                        int nx, int ny, int ignore_monster_id);
[[nodiscard]] bool canNpcOccupyTile(const ServerState& state, const NpcRuntime& npc,
                                    int nx, int ny, int ignore_npc_id);

// ---- Spatial search ----
[[nodiscard]] bool findNearestPlayerToMonster(const ServerState& state, const MonsterRuntime& mon,
                                              int& out_x, int& out_y);
[[nodiscard]] bool findRespawnTile(const ServerState& state, const std::string& room_name,
                                   int& out_x, int& out_y);

// ---- Distance and reachability ----
[[nodiscard]] constexpr int tileDistance(int ax, int ay, int bx, int by) {
    const int dx = (ax > bx) ? (ax - bx) : (bx - ax);
    const int dy = (ay > by) ? (ay - by) : (by - ay);
    return dx + dy;
}

[[nodiscard]] bool isItemReachableByPlayer(const PlayerRuntime& p, const GroundItemRuntime& item);
[[nodiscard]] int findGroundItemIndexById(const ServerState& state, const PlayerRuntime& p, int item_id);
[[nodiscard]] int findPickupCandidateIndex(const ServerState& state, const PlayerRuntime& p, int requested_item_id);
