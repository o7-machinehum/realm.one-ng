#include "monster_combat.h"

#include <algorithm>
#include <cstdlib>

namespace {

// Maps a movement/target delta to the protocol facing index.
int facingFromDelta(int dx, int dy, int fallback_facing) {
    if (dx > 0) return 1;
    if (dx < 0) return 3;
    if (dy > 0) return 2;
    if (dy < 0) return 0;
    return fallback_facing;
}

} // namespace

MonsterCombatResult resolveMonsterAttacks(
    std::unordered_map<ENetPeer*, PlayerRuntime>& players,
    std::vector<MonsterRuntime>& monsters,
    const std::function<bool(const std::string&, int&, int&)>& find_respawn_tile,
    int melee_range_tiles
) {
    MonsterCombatResult out;
    for (auto& mon : monsters) {
        if (mon.hp <= 0) continue;

        ENetPeer* target_peer = nullptr;
        int best_dist = 999999;
        for (auto& [peer, p] : players) {
            if (!p.authenticated) continue;
            if (p.data.room != mon.room) continue;
            const int dist = std::abs(mon.x - p.data.x) + std::abs(mon.y - p.data.y);
            if (dist > melee_range_tiles) continue;
            if (dist >= best_dist) continue;
            best_dist = dist;
            target_peer = peer;
        }
        if (!target_peer) continue;

        auto pit = players.find(target_peer);
        if (pit == players.end()) continue;
        auto& p = pit->second;

        mon.facing = facingFromDelta(p.data.x - mon.x, p.data.y - mon.y, mon.facing);
        mon.attack_anim_seq += 1;

        const int damage = std::max(1, mon.strength);
        p.hp = std::max(0, p.hp - damage);
        out.changed = true;
        if (out.event_text.empty()) {
            out.event_text = mon.name + " hits " + p.data.username + " for " + std::to_string(damage);
        }

        if (p.hp <= 0) {
            p.attack_target_monster_id = -1;
            p.hp = p.max_hp;
            p.mana = p.max_mana;
            int rx = 0;
            int ry = 0;
            if (find_respawn_tile(p.data.room, rx, ry)) {
                p.data.x = rx;
                p.data.y = ry;
            }
            out.defeated_players.push_back(target_peer);
            out.event_text = p.data.username + " was defeated by " + mon.name;
        }
    }
    return out;
}
