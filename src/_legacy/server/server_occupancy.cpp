#include "server_occupancy.h"

#include "server_constants.h"
#include "world.h"

#include <algorithm>
#include <cstdlib>

// ---- Tile occupancy ----

bool isTileOccupiedByMonster(const ServerState& state, const std::string& room, TilePos pos) {
    for (const auto& m : state.monsters) {
        if (m.room != room) continue;
        if (m.hp <= 0) continue;
        if (m.pos == pos) return true;
    }
    return false;
}

bool isTileOccupiedByNpc(const ServerState& state, const std::string& room, TilePos pos) {
    for (const auto& n : state.npcs) {
        if (n.room != room) continue;
        if (n.pos == pos) return true;
    }
    return false;
}

bool isTileOccupiedByPlayer(const ServerState& state, ENetPeer* exclude,
                             const std::string& room, TilePos pos) {
    for (const auto& [peer, p] : state.players) {
        if (peer == exclude) continue;
        if (!p.authenticated) continue;
        if (p.data.room == room && p.data.pos == pos) return true;
    }
    return false;
}

// ---- Movement validation ----

bool canMonsterOccupyTile(const ServerState& state, const MonsterRuntime& mon,
                           TilePos target, int ignore_monster_id) {
    const Room* room = state.world->getRoom(mon.room);
    if (!room) return false;
    if (!room->isWalkable(target.x, target.y)) return false;
    for (const auto& [_, p] : state.players) {
        if (!p.authenticated) continue;
        if (p.data.room != mon.room) continue;
        if (p.data.pos == target) return false;
    }
    for (const auto& n : state.npcs) {
        if (n.room != mon.room) continue;
        if (n.pos == target) return false;
    }
    for (const auto& other : state.monsters) {
        if (other.id == ignore_monster_id) continue;
        if (other.room != mon.room || other.hp <= 0) continue;
        if (other.pos == target) return false;
    }
    return true;
}

bool canNpcOccupyTile(const ServerState& state, const NpcRuntime& npc,
                       TilePos target, int ignore_npc_id) {
    const Room* room = state.world->getRoom(npc.room);
    if (!room) return false;
    if (!room->isWalkable(target.x, target.y)) return false;
    if (target.distanceTo(npc.home_pos) > std::max(0, npc.wander_radius)) return false;
    for (const auto& [_, p] : state.players) {
        if (!p.authenticated) continue;
        if (p.data.room != npc.room) continue;
        if (p.data.pos == target) return false;
    }
    for (const auto& m : state.monsters) {
        if (m.room != npc.room || m.hp <= 0) continue;
        if (m.pos == target) return false;
    }
    for (const auto& other : state.npcs) {
        if (other.id == ignore_npc_id) continue;
        if (other.room != npc.room) continue;
        if (other.pos == target) return false;
    }
    return true;
}

// ---- Spatial search ----

std::optional<TilePos> findNearestPlayerToMonster(const ServerState& state,
                                                   const MonsterRuntime& mon) {
    int best_dist = 999999;
    std::optional<TilePos> result;
    for (const auto& [_, p] : state.players) {
        if (!p.authenticated) continue;
        if (p.data.room != mon.room) continue;
        const int dist = mon.pos.distanceTo(p.data.pos);
        if (dist >= best_dist) continue;
        best_dist = dist;
        result = p.data.pos;
    }
    return result;
}

std::optional<TilePos> findRespawnTile(const ServerState& state,
                                        const std::string& room_name) {
    const Room* room = state.world->getRoom(room_name);
    if (!room) return std::nullopt;

    const TilePos fallback{std::clamp(2, 0, room->map_width() - 1),
                           std::clamp(2, 0, room->map_height() - 1)};
    if (room->isWalkable(fallback.x, fallback.y) &&
        !isTileOccupiedByMonster(state, room_name, fallback) &&
        !isTileOccupiedByNpc(state, room_name, fallback)) {
        return fallback;
    }

    for (int y = 0; y < room->map_height(); ++y) {
        for (int x = 0; x < room->map_width(); ++x) {
            if (!room->isWalkable(x, y)) continue;
            TilePos candidate{x, y};
            if (isTileOccupiedByMonster(state, room_name, candidate)) continue;
            if (isTileOccupiedByNpc(state, room_name, candidate)) continue;
            return candidate;
        }
    }
    return std::nullopt;
}

// ---- Distance and reachability ----

bool isItemReachableByPlayer(const PlayerRuntime& p, const GroundItemRuntime& item) {
    if (item.room != p.data.room) return false;
    return p.data.pos.chebyshevTo(item.pos) <= kMeleeRangeTiles;
}

int findGroundItemIndexById(const ServerState& state, const PlayerRuntime& p, int item_id) {
    for (size_t i = 0; i < state.items.size(); ++i) {
        if (state.items[i].id != item_id) continue;
        if (state.items[i].room != p.data.room) continue;
        return static_cast<int>(i);
    }
    return -1;
}

int findPickupCandidateIndex(const ServerState& state, const PlayerRuntime& p, int requested_item_id) {
    if (requested_item_id != -1) {
        const int idx = findGroundItemIndexById(state, p, requested_item_id);
        if (idx < 0) return -1;
        return isItemReachableByPlayer(p, state.items[static_cast<size_t>(idx)]) ? idx : -1;
    }

    int best_idx = -1;
    int best_dist = 999999;
    for (size_t i = 0; i < state.items.size(); ++i) {
        const auto& item = state.items[i];
        if (!isItemReachableByPlayer(p, item)) continue;
        const int dist = tileDistance(p.data.pos, item.pos);
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = static_cast<int>(i);
        }
    }
    return best_idx;
}
