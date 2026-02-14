#include "server_occupancy.h"

#include "world.h"

#include <algorithm>
#include <cstdlib>

namespace {
constexpr int kMeleeRangeTiles = 1;
} // namespace

// ---- Tile occupancy ----

bool isTileOccupiedByMonster(const ServerState& state, const std::string& room, int x, int y) {
    for (const auto& m : state.monsters) {
        if (m.room != room) continue;
        if (m.hp <= 0) continue;
        if (m.x == x && m.y == y) return true;
    }
    return false;
}

bool isTileOccupiedByNpc(const ServerState& state, const std::string& room, int x, int y) {
    for (const auto& n : state.npcs) {
        if (n.room != room) continue;
        if (n.x == x && n.y == y) return true;
    }
    return false;
}

bool isTileOccupiedByPlayer(const ServerState& state, ENetPeer* exclude,
                             const std::string& room, int x, int y) {
    for (const auto& [peer, p] : state.players) {
        if (peer == exclude) continue;
        if (!p.authenticated) continue;
        if (p.data.room == room && p.data.x == x && p.data.y == y) return true;
    }
    return false;
}

// ---- Movement validation ----

bool canMonsterOccupyTile(const ServerState& state, const MonsterRuntime& mon,
                           int nx, int ny, int ignore_monster_id) {
    const Room* room = state.world->getRoom(mon.room);
    if (!room) return false;
    if (!room->isWalkable(nx, ny)) return false;
    for (const auto& [_, p] : state.players) {
        if (!p.authenticated) continue;
        if (p.data.room != mon.room) continue;
        if (p.data.x == nx && p.data.y == ny) return false;
    }
    for (const auto& n : state.npcs) {
        if (n.room != mon.room) continue;
        if (n.x == nx && n.y == ny) return false;
    }
    for (const auto& other : state.monsters) {
        if (other.id == ignore_monster_id) continue;
        if (other.room != mon.room || other.hp <= 0) continue;
        if (other.x == nx && other.y == ny) return false;
    }
    return true;
}

bool canNpcOccupyTile(const ServerState& state, const NpcRuntime& npc,
                       int nx, int ny, int ignore_npc_id) {
    const Room* room = state.world->getRoom(npc.room);
    if (!room) return false;
    if (!room->isWalkable(nx, ny)) return false;
    if (std::abs(nx - npc.home_x) + std::abs(ny - npc.home_y) > std::max(0, npc.wander_radius)) return false;
    for (const auto& [_, p] : state.players) {
        if (!p.authenticated) continue;
        if (p.data.room != npc.room) continue;
        if (p.data.x == nx && p.data.y == ny) return false;
    }
    for (const auto& m : state.monsters) {
        if (m.room != npc.room || m.hp <= 0) continue;
        if (m.x == nx && m.y == ny) return false;
    }
    for (const auto& other : state.npcs) {
        if (other.id == ignore_npc_id) continue;
        if (other.room != npc.room) continue;
        if (other.x == nx && other.y == ny) return false;
    }
    return true;
}

// ---- Spatial search ----

bool findNearestPlayerToMonster(const ServerState& state, const MonsterRuntime& mon,
                                 int& out_x, int& out_y) {
    int best_dist = 999999;
    bool found = false;
    for (const auto& [_, p] : state.players) {
        if (!p.authenticated) continue;
        if (p.data.room != mon.room) continue;
        const int dist = std::abs(mon.x - p.data.x) + std::abs(mon.y - p.data.y);
        if (dist >= best_dist) continue;
        best_dist = dist;
        out_x = p.data.x;
        out_y = p.data.y;
        found = true;
    }
    return found;
}

bool findRespawnTile(const ServerState& state, const std::string& room_name,
                      int& out_x, int& out_y) {
    const Room* room = state.world->getRoom(room_name);
    if (!room) return false;

    const int fallback_x = std::clamp(2, 0, room->map_width() - 1);
    const int fallback_y = std::clamp(2, 0, room->map_height() - 1);
    if (room->isWalkable(fallback_x, fallback_y) &&
        !isTileOccupiedByMonster(state, room_name, fallback_x, fallback_y) &&
        !isTileOccupiedByNpc(state, room_name, fallback_x, fallback_y)) {
        out_x = fallback_x;
        out_y = fallback_y;
        return true;
    }

    for (int y = 0; y < room->map_height(); ++y) {
        for (int x = 0; x < room->map_width(); ++x) {
            if (!room->isWalkable(x, y)) continue;
            if (isTileOccupiedByMonster(state, room_name, x, y)) continue;
            if (isTileOccupiedByNpc(state, room_name, x, y)) continue;
            out_x = x;
            out_y = y;
            return true;
        }
    }
    return false;
}

// ---- Distance and reachability ----

bool isItemReachableByPlayer(const PlayerRuntime& p, const GroundItemRuntime& item) {
    if (item.room != p.data.room) return false;
    const int dx = std::abs(p.data.x - item.x);
    const int dy = std::abs(p.data.y - item.y);
    return std::max(dx, dy) <= kMeleeRangeTiles;
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
        const int dist = tileDistance(p.data.x, p.data.y, item.x, item.y);
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = static_cast<int>(i);
        }
    }
    return best_idx;
}
