#include "combat.h"

// ---- Derived player combat stats ----
int computePlayerOffence(const PlayerRuntime& p, const ServerState& state) {
    const int melee = computeLevelFromXp(state.settings.progression, p.data.melee_xp).level;
    const int equip = computeEquippedStatBonus(p.data, state.item_instances, state.item_defs_by_id, &ItemDef::attack);
    return std::max(1, melee + equip);
}

int computePlayerDefence(const PlayerRuntime& p, const ServerState& state) {
    const int skill = computeLevelFromXp(state.settings.progression, p.data.shielding_xp).level;
    return std::max(1, skill);
}

int computePlayerArmor(const PlayerRuntime& p, const ServerState& state) {
    const int equip = computeEquippedStatBonus(p.data, state.item_instances, state.item_defs_by_id, &ItemDef::defense);
    return std::max(0, equip);
}

int computePlayerEvasion(const PlayerRuntime& p, const ServerState& state) {
    const int skill = computeLevelFromXp(state.settings.progression, p.data.evasion_xp).level;
    const int equip = computeEquippedStatBonus(p.data, state.item_instances, state.item_defs_by_id, &ItemDef::evasion);
    return std::max(1, skill + equip);
}

bool eval_death(ServerState& state, PlayerRuntime& p, MonsterRuntime& mon, TickResult& result) {
    // If the monster is dead
    if (mon.hp <= 0) {
        p.data.exp += mon.exp_reward;
        p.data.melee_xp += std::max(1, mon.exp_reward / 2);
        p.xp_gain_amount = mon.exp_reward;
        p.xp_gain_seq += 1;
        p.attack_target_monster_id = -1;

        // Drop corpse item
        const std::string corpse_def_id = makeCorpseItemId(mon.def_id.empty() ? mon.name : mon.def_id);
        GroundItemRuntime corpse{};
        corpse.id = state.next_item_id++;
        corpse.instance_id = allocateItemInstance(state, corpse_def_id);
        corpse.item_id = corpse_def_id;
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
        persistPlayer(p, *state.auth_db);
        return true;
    }
    return false;
}

void melee_combat(ServerState& state, PlayerRuntime& p, MonsterRuntime& mon, TickResult& result) {
    if (!p.authenticated) return;
    if (p.attack_target_monster_id < 0) return;


    const int dist = mon.pos.distanceTo(p.data.pos);
    if (dist > kMeleeRangeTiles) return;

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
        return;
    }
    if (outcome == CombatOutcome::Blocked) {
        applyCombatOutcome(p, CombatOutcome::None);
        applyCombatOutcome(mon, CombatOutcome::Blocked);
        p.data.shielding_xp += 1;
        result.state_changed = true;
        result.event_text = mon.name + " blocked " + p.data.username;
        return;
    }

    const int dmg = std::max(1, 2 + melee_level / 2 + offence / 2 - mon.defense / 3);
    applyCombatOutcome(p, CombatOutcome::None);
    applyCombatHit(mon, dmg);
    mon.hp = std::max(0, mon.hp - dmg);
    p.data.melee_xp += 3;
    result.state_changed = true;
    result.event_text = p.data.username + " hit " + mon.name + " for " + std::to_string(dmg);
}

void combat(ServerState& state, PlayerRuntime& player, TickResult& result) {
    // Find which monster the player is attacking
    int hit_index = -1;
    for (size_t i = 0; i < state.monsters.size(); ++i) {
        if (state.monsters[i].id == player.attack_target_monster_id) {
            hit_index = static_cast<int>(i);
            break;
        }
    }

    if (hit_index < 0) {
        player.attack_target_monster_id = -1;
        result.state_changed = true;
        result.event_text = player.data.username + " lost target";
        return;
    }

    auto& monster = state.monsters[hit_index];
    if (monster.room != player.data.room) return;

    // In the future this might be melee, or ranged, etc.
    melee_combat(state, player, monster, result);

    /* Determine if monster is dead or not */
    if(eval_death(state, player, monster, result)) {
        state.monsters.erase(state.monsters.begin() + hit_index);
    }
}
