// Tile occupancy checks and spatial queries used by movement, spawning, and combat.
#pragma once

#include "server_state.h"
#include "tile_pos.h"

#include <optional>
#include <string>

// ---- Tile occupancy ----

// Returns true if any living monster occupies the given tile.
[[nodiscard]] bool isTileOccupiedByMonster(const ServerState& state, const std::string& room, TilePos pos);

// Returns true if any NPC occupies the given tile.
[[nodiscard]] bool isTileOccupiedByNpc(const ServerState& state, const std::string& room, TilePos pos);

// Returns true if any authenticated player (except `exclude`) occupies the given tile.
[[nodiscard]] bool isTileOccupiedByPlayer(const ServerState& state, ENetPeer* exclude,
                                          const std::string& room, TilePos pos);

// ---- Movement validation ----

// Returns true if a monster can step onto the target tile (walkable, no collisions).
[[nodiscard]] bool canMonsterOccupyTile(const ServerState& state, const MonsterRuntime& mon,
                                        TilePos target, int ignore_monster_id);

// Returns true if an NPC can step onto the target tile (walkable, within wander radius, no collisions).
[[nodiscard]] bool canNpcOccupyTile(const ServerState& state, const NpcRuntime& npc,
                                    TilePos target, int ignore_npc_id);

// ---- Spatial search ----

// Finds the nearest authenticated player to the given monster. Returns their tile position.
[[nodiscard]] std::optional<TilePos> findNearestPlayerToMonster(const ServerState& state,
                                                                const MonsterRuntime& mon);

// Finds a walkable, unoccupied tile in the given room for respawning.
[[nodiscard]] std::optional<TilePos> findRespawnTile(const ServerState& state,
                                                     const std::string& room_name);

// ---- Distance and reachability ----

// Manhattan distance between two tile positions.
[[nodiscard]] constexpr int tileDistance(TilePos a, TilePos b) {
    const int dx = (a.x > b.x) ? (a.x - b.x) : (b.x - a.x);
    const int dy = (a.y > b.y) ? (a.y - b.y) : (b.y - a.y);
    return dx + dy;
}

// Returns true if the player can reach the ground item (within melee range, same room).
[[nodiscard]] bool isItemReachableByPlayer(const PlayerRuntime& p, const GroundItemRuntime& item);

// Returns the index of a ground item with the given runtime ID in the player's room, or -1.
[[nodiscard]] int findGroundItemIndexById(const ServerState& state, const PlayerRuntime& p, int item_id);

// Returns the best pickup candidate index for the player, or -1 if none found.
[[nodiscard]] int findPickupCandidateIndex(const ServerState& state, const PlayerRuntime& p, int requested_item_id);
