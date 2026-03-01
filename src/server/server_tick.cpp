#include "server_tick.h"

#include "server_constants.h"
#include "server_occupancy.h"
#include "server_spawning.h"
#include "server_util.h"
#include "world.h"

#include <algorithm>
#include <array>
#include <random>

namespace {

std::mt19937& tickRng() {
    static std::mt19937 rng{std::random_device{}()};
    return rng;
}

int randomDir4() {
    std::uniform_int_distribution<int> dist(0, 3);
    return dist(tickRng());
}

bool tryMoveMonster(const ServerState& state, MonsterRuntime& mon,
                    const std::array<std::pair<int, int>, 4>& dirs) {
    for (const auto& [dx, dy] : dirs) {
        if (dx == 0 && dy == 0) continue;
        const TilePos target = mon.pos.offset(dx, dy);
        if (!canMonsterOccupyTile(state, mon, target, mon.id)) continue;
        mon.facing = facingFromDelta(dx, dy, mon.facing);
        mon.pos = target;
        return true;
    }
    return false;
}

} // namespace

TickResult advanceServerTick(ServerState& state, int tick_ms) {
    TickResult result;

    // ---- Section: Update player vitals ----
    for (auto& [_, p] : state.players) {
        if (!p.authenticated) continue;
        const int before_max = p.max_hp;
        const int before_hp = p.hp;
        updatePlayerVitalsFromLevel(p, state);
        if (p.max_hp != before_max || p.hp != before_hp) result.state_changed = true;
    }

    // ---- Section: Player-vs-monster melee combat ----
    for (auto& [_, p] : state.players) {
        if (!p.authenticated) continue;
        if (p.attack_target_monster_id < 0) continue;

        int hit_index = -1;
        for (size_t i = 0; i < state.monsters.size(); ++i) {
            if (state.monsters[i].id == p.attack_target_monster_id) {
                hit_index = static_cast<int>(i);
                break;
            }
        }

        if (hit_index < 0) {
            p.attack_target_monster_id = -1;
            result.state_changed = true;
            result.event_text = p.data.username + " lost target";
            continue;
        }

        auto& mon = state.monsters[hit_index];
        if (mon.room != p.data.room) continue;

        const int dist = mon.pos.distanceTo(p.data.pos);
        if (dist > kMeleeRangeTiles) continue;

        p.facing = facingFromDelta(mon.pos.x - p.data.pos.x, mon.pos.y - p.data.pos.y, p.facing);
        p.attack_anim_seq += 1;
        mon.facing = facingFromDelta(p.data.pos.x - mon.pos.x, p.data.pos.y - mon.pos.y, mon.facing);
        mon.attack_anim_seq += 1;

        const int melee_level = computeLevelFromXp(state.settings.progression, p.data.melee_xp).level;
        const int offence = computePlayerOffence(p, state);
        const int miss_chance = std::clamp(18 + mon.evasion * 2 - melee_level - mon.accuracy / 8, 3, 70);
        const int block_chance = std::clamp(mon.block_chance + mon.defense - offence / 4, 0, 75);

        CombatOutcome outcome = CombatOutcome::Hit;
        if (rollPercentChance(miss_chance)) outcome = CombatOutcome::Missed;
        else if (rollPercentChance(block_chance)) outcome = CombatOutcome::Blocked;

        if (outcome == CombatOutcome::Missed) {
            applyCombatOutcome(p, CombatOutcome::Missed);
            applyCombatOutcome(mon, CombatOutcome::None);
            result.state_changed = true;
            result.event_text = p.data.username + " whiffed " + mon.name;
            continue;
        }
        if (outcome == CombatOutcome::Blocked) {
            applyCombatOutcome(p, CombatOutcome::None);
            applyCombatOutcome(mon, CombatOutcome::Blocked);
            p.data.shielding_xp += 1;
            result.state_changed = true;
            result.event_text = mon.name + " blocked " + p.data.username;
            continue;
        }

        const int dmg = std::max(1, 2 + melee_level / 2 + offence / 2 - mon.defense / 3);
        applyCombatOutcome(p, CombatOutcome::None);
        applyCombatHit(mon, dmg);
        mon.hp = std::max(0, mon.hp - dmg);
        p.data.melee_xp += 3;
        result.state_changed = true;
        result.event_text = p.data.username + " hit " + mon.name + " for " + std::to_string(dmg);

        // If the monster is dead
        if (mon.hp <= 0) {
            p.data.exp += mon.exp_reward;
            p.data.melee_xp += std::max(1, mon.exp_reward / 2);
            p.attack_target_monster_id = -1;

            // Drop corpse item
            GroundItemRuntime corpse{};
            corpse.id = state.next_item_id++;
            corpse.item_id = makeCorpseItemId(mon.def_id.empty() ? mon.name : mon.def_id);
            corpse.name = mon.name + " corpse";
            corpse.sprite_tileset = mon.sprite_tileset;
            corpse.sprite_name = mon.sprite_name;
            corpse.sprite_w_tiles = std::max(1, mon.size_w);
            corpse.sprite_h_tiles = std::max(1, mon.size_h);
            corpse.sprite_clip = 1;
            corpse.room = mon.room;
            corpse.pos = mon.pos;
            state.items.push_back(std::move(corpse));

            // Roll loot drops
            for (const auto& drop : mon.drops) {
                if (drop.item_id.empty()) continue;
                if (!rollFloatChance(drop.chance)) continue;
                spawnGroundItem(state, drop.item_id, mon.room, mon.pos);
            }

            state.pending_respawns.push_back(MonsterRespawnEntry{
                mon,
                std::max(0, state.settings.gameplay.monster_respawn_ms)
            });
            result.event_text = p.data.username + " killed " + mon.name +
                                " (exp +" + std::to_string(mon.exp_reward) + ")";
            state.monsters.erase(state.monsters.begin() + hit_index);
            persistPlayer(p, *state.auth_db);
        }
    }

    // ---- Section: Monster AI movement (chase + wander) ----
    for (auto& mon : state.monsters) {
        if (mon.hp <= 0) continue;
        mon.move_accum_ms += tick_ms;
        if (mon.move_accum_ms < std::max(1, mon.speed_ms)) continue;
        mon.move_accum_ms = 0;

        constexpr std::array<std::pair<int, int>, 4> dirs = {{
            {0, -1}, {1, 0}, {0, 1}, {-1, 0}
        }};
        const int start = randomDir4();
        std::array<std::pair<int, int>, 4> random_dirs = {{
            dirs[(start + 0) % 4],
            dirs[(start + 1) % 4],
            dirs[(start + 2) % 4],
            dirs[(start + 3) % 4]
        }};

        bool moved = false;
        auto player_pos = findNearestPlayerToMonster(state, mon);
        if (player_pos.has_value()) {
            std::array<std::pair<int, int>, 4> chase_dirs{};
            int idx = 0;
            auto push_unique = [&](int dx, int dy) {
                if (dx == 0 && dy == 0) return;
                for (int i = 0; i < idx; ++i) {
                    if (chase_dirs[static_cast<size_t>(i)].first == dx &&
                        chase_dirs[static_cast<size_t>(i)].second == dy) return;
                }
                if (idx < 4) chase_dirs[static_cast<size_t>(idx++)] = {dx, dy};
            };

            const int dx_to_player = player_pos->x - mon.pos.x;
            const int dy_to_player = player_pos->y - mon.pos.y;
            const int sx = signum(dx_to_player);
            const int sy = signum(dy_to_player);
            if (std::abs(dx_to_player) >= std::abs(dy_to_player)) {
                push_unique(sx, 0);
                push_unique(0, sy);
            } else {
                push_unique(0, sy);
                push_unique(sx, 0);
            }
            for (const auto& [rdx, rdy] : random_dirs) push_unique(rdx, rdy);
            moved = tryMoveMonster(state, mon, chase_dirs);
        } else {
            // 25% chance to idle this tick to avoid jittery movement.
            std::uniform_int_distribution<int> idle_dist(0, kMonsterIdleChanceDivisor - 1);
            if (idle_dist(tickRng()) != 0) {
                moved = tryMoveMonster(state, mon, random_dirs);
            }
        }

        if (moved) {
            result.state_changed = true;
            if (result.event_text.empty()) result.event_text = "monsters moved";
        }
    }

    // ---- Section: NPC wandering ----
    for (auto& npc : state.npcs) {
        if (npc.talk_pause_ms > 0) {
            npc.talk_pause_ms = std::max(0, npc.talk_pause_ms - tick_ms);
            continue;
        }
        npc.move_accum_ms += tick_ms;
        if (npc.move_accum_ms < std::max(1, npc.speed_ms)) continue;
        npc.move_accum_ms = 0;

        constexpr std::array<std::pair<int, int>, 4> dirs = {{
            {0, -1}, {1, 0}, {0, 1}, {-1, 0}
        }};
        const int start = randomDir4();
        std::array<std::pair<int, int>, 4> random_dirs = {{
            dirs[(start + 0) % 4],
            dirs[(start + 1) % 4],
            dirs[(start + 2) % 4],
            dirs[(start + 3) % 4]
        }};
        bool moved = false;
        for (const auto& [dx, dy] : random_dirs) {
            const TilePos target = npc.pos.offset(dx, dy);
            if (!canNpcOccupyTile(state, npc, target, npc.id)) continue;
            npc.facing = facingFromDelta(dx, dy, npc.facing);
            npc.pos = target;
            moved = true;
            break;
        }
        if (moved) {
            result.state_changed = true;
            if (result.event_text.empty()) result.event_text = "npcs moved";
        }
    }

    // ---- Section: Monster-vs-player melee combat ----
    for (auto& mon : state.monsters) {
        if (mon.hp <= 0) continue;

        ENetPeer* target_peer = nullptr;
        int best_dist = 999999;
        for (auto& [peer, p] : state.players) {
            if (!p.authenticated) continue;
            if (p.data.room != mon.room) continue;
            const int dist = mon.pos.distanceTo(p.data.pos);
            if (dist > kMeleeRangeTiles) continue;
            if (dist >= best_dist) continue;
            best_dist = dist;
            target_peer = peer;
        }
        if (!target_peer) continue;
        auto pit = state.players.find(target_peer);
        if (pit == state.players.end()) continue;
        auto& p = pit->second;

        mon.facing = facingFromDelta(p.data.pos.x - mon.pos.x, p.data.pos.y - mon.pos.y, mon.facing);
        mon.attack_anim_seq += 1;

        const int defence = computePlayerDefence(p, state);
        const int armor = computePlayerArmor(p, state);
        const int evasion = computePlayerEvasion(p, state);
        const int miss_chance = std::clamp(18 + evasion * 2 - mon.accuracy / 2, 3, 70);
        const int block_chance_val = std::clamp(8 + defence * 3 - mon.strength, 0, 80);

        CombatOutcome outcome = CombatOutcome::Hit;
        if (rollPercentChance(miss_chance)) outcome = CombatOutcome::Missed;
        else if (rollPercentChance(block_chance_val)) outcome = CombatOutcome::Blocked;

        if (outcome == CombatOutcome::Missed) {
            applyCombatOutcome(mon, CombatOutcome::None);
            applyCombatOutcome(p, CombatOutcome::None);
            p.data.evasion_xp += 1;
            result.state_changed = true;
            if (result.event_text.empty()) result.event_text = mon.name + " missed " + p.data.username;
            continue;
        }
        if (outcome == CombatOutcome::Blocked) {
            applyCombatOutcome(mon, CombatOutcome::None);
            applyCombatOutcome(p, CombatOutcome::Blocked);
            p.data.shielding_xp += 2;
            result.state_changed = true;
            if (result.event_text.empty()) result.event_text = p.data.username + " blocked " + mon.name;
            continue;
        }

        applyCombatOutcome(mon, CombatOutcome::None);

        const int raw = std::max(1, mon.strength + mon.accuracy / 10);
        const int reduced = raw - std::max(0, armor / 2);
        const int damage = std::max(1, reduced);
        applyCombatHit(p, damage);
        p.hp = std::max(0, p.hp - damage);
        result.state_changed = true;
        if (result.event_text.empty()) {
            result.event_text = mon.name + " hit " + p.data.username + " for " + std::to_string(damage);
        }

        // ---- Player Death ---- //
        if (p.hp <= 0) {
            p.attack_target_monster_id = -1;
            p.hp = p.max_hp;
            p.mana = p.max_mana;
            if (auto respawn = findRespawnTile(state, p.data.room)) {
                p.data.pos = *respawn;
            }
            persistPlayer(p, *state.auth_db);
            result.event_text = p.data.username + " was defeated by " + mon.name;
        }
    }

    // ---- Section: Monster respawn management ----
    for (size_t i = 0; i < state.pending_respawns.size();) {
        auto& entry = state.pending_respawns[i];
        entry.remaining_ms -= tick_ms;
        if (entry.remaining_ms > 0) {
            ++i;
            continue;
        }

        MonsterRuntime respawned = entry.mon;
        const TilePos rpos = respawned.spawn_pos;
        const std::string room_name = respawned.room;
        const Room* room = state.world->getRoom(room_name);
        if (!room) {
            state.pending_respawns.erase(state.pending_respawns.begin() + static_cast<long>(i));
            continue;
        }
        if (!room->isWalkable(rpos.x, rpos.y) ||
            isTileOccupiedByMonster(state, room_name, rpos) ||
            isTileOccupiedByNpc(state, room_name, rpos) ||
            isTileOccupiedByPlayer(state, nullptr, room_name, rpos)) {
            entry.remaining_ms = 1000;
            ++i;
            continue;
        }

        respawned.id = state.next_monster_id++;
        respawned.pos = rpos;
        respawned.hp = respawned.max_hp;
        respawned.facing = Facing::South;
        respawned.attack_anim_seq = 0;
        respawned.combat_outcome = CombatOutcome::None;
        respawned.combat_outcome_seq = 0;
        respawned.combat_value = 0;
        respawned.move_accum_ms = 0;
        state.monsters.push_back(std::move(respawned));
        state.pending_respawns.erase(state.pending_respawns.begin() + static_cast<long>(i));
        result.state_changed = true;
        if (result.event_text.empty()) result.event_text = "a monster respawned";
    }

    return result;
}
