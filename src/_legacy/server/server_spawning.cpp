#include "server_spawning.h"

#include "server_util.h"
#include "string_util.h"
#include "world.h"

#include <algorithm>
#include <iostream>

void spawnGroundItem(ServerState& state, const std::string& raw_item_id,
                     const std::string& room_name, TilePos pos) {
    const std::string item_id = normalizeId(raw_item_id);
    auto dit = state.item_defs_by_id.find(item_id);
    if (dit == state.item_defs_by_id.end()) {
        std::cerr << "[server] unknown item id '" << raw_item_id
                  << "' in room " << room_name << "\n";
        return;
    }
    const ItemDef& def = *dit->second;
    GroundItemRuntime item{};
    item.id = state.next_item_id++;
    item.instance_id = allocateItemInstance(state, def.id);
    item.item_id = def.id;
    item.name = def.name;
    item.sprite_tileset = def.sprite_tileset;
    item.sprite_name = def.id;
    item.sprite_w_tiles = 1;
    item.sprite_h_tiles = 1;
    item.sprite_clip = 0;
    item.room = room_name;
    item.pos = pos;
    state.items.push_back(std::move(item));
}

void spawnInitialEntities(ServerState& state) {
    for (const auto& room_name : state.world->roomNames()) {
        const Room* room = state.world->getRoom(room_name);
        if (!room) continue;

        // ---- Spawn monsters ----
        for (const auto& spawn : room->monster_spawns()) {
            auto dit = state.monster_defs_by_id.find(normalizeId(spawn.monster_id));
            if (dit == state.monster_defs_by_id.end()) {
                std::cerr << "[server] unknown monster id '" << spawn.monster_id
                          << "' in room " << room_name << "\n";
                continue;
            }
            const MonsterDef& def = *dit->second;
            const int sw = std::max(1, def.monster_size_w);
            const int sh = std::max(1, def.monster_size_h);
            if (!room->isWalkable(spawn.pos.x, spawn.pos.y)) {
                std::cerr << "[server] monster spawn '" << spawn.monster_id
                          << "' overlaps blocked anchor tile in room "
                          << room_name << " at (" << spawn.pos.x << "," << spawn.pos.y << ")\n";
                continue;
            }
            bool overlap = false;
            for (const auto& m : state.monsters) {
                if (m.room != room_name || m.hp <= 0) continue;
                if (m.pos == spawn.pos) { overlap = true; break; }
            }
            if (overlap) {
                std::cerr << "[server] overlapping monster spawn in room "
                          << room_name << " at (" << spawn.pos.x << "," << spawn.pos.y << ")\n";
                continue;
            }

            MonsterRuntime mon{};
            mon.id = state.next_monster_id++;
            mon.def_id = def.id;
            mon.name = spawn.name_override.empty() ? def.name : spawn.name_override;
            mon.sprite_tileset = def.sprite_tileset;
            mon.sprite_name = def.id;
            mon.room = room_name;
            mon.pos = spawn.pos;
            mon.spawn_pos = spawn.pos;
            mon.size_w = sw;
            mon.size_h = sh;
            mon.hp = def.max_hp;
            mon.max_hp = def.max_hp;
            mon.strength = std::max(1, def.strength);
            mon.defense = std::max(0, def.defense);
            mon.evasion = std::max(0, def.evasion);
            mon.accuracy = std::clamp(def.accuracy, 1, 100);
            mon.block_chance = std::clamp(def.block_chance, 0, 95);
            mon.facing = Facing::South;
            mon.attack_anim_seq = 0;
            mon.combat_outcome = CombatOutcome::None;
            mon.combat_outcome_seq = 0;
            mon.speed_ms = std::max(1, def.speed_ms);
            mon.move_accum_ms = 0;
            mon.exp_reward = def.exp_reward;
            mon.drops = def.drops;
            state.monsters.push_back(std::move(mon));
        }

        // ---- Spawn ground items ----
        for (const auto& spawn : room->item_spawns()) {
            if (!room->isWalkable(spawn.pos.x, spawn.pos.y)) {
                std::cerr << "[server] item spawn '" << spawn.item_id
                          << "' overlaps blocked tile in room "
                          << room_name << " at (" << spawn.pos.x << "," << spawn.pos.y << ")\n";
                continue;
            }
            spawnGroundItem(state, spawn.item_id, room_name, spawn.pos);
        }

        // ---- Spawn NPCs ----
        for (const auto& spawn : room->npc_spawns()) {
            auto dit = state.npc_defs_by_id.find(normalizeId(spawn.npc_id));
            if (dit == state.npc_defs_by_id.end()) {
                std::cerr << "[server] unknown npc id '" << spawn.npc_id
                          << "' in room " << room_name << "\n";
                continue;
            }
            const NpcDef& def = *dit->second;
            if (!room->isWalkable(spawn.pos.x, spawn.pos.y)) {
                std::cerr << "[server] npc spawn '" << spawn.npc_id
                          << "' overlaps blocked anchor tile in room "
                          << room_name << " at (" << spawn.pos.x << "," << spawn.pos.y << ")\n";
                continue;
            }
            bool overlap = false;
            for (const auto& m : state.monsters) {
                if (m.room != room_name || m.hp <= 0) continue;
                if (m.pos == spawn.pos) { overlap = true; break; }
            }
            if (!overlap) {
                for (const auto& n : state.npcs) {
                    if (n.room != room_name) continue;
                    if (n.pos == spawn.pos) { overlap = true; break; }
                }
            }
            if (overlap) {
                std::cerr << "[server] overlapping npc spawn in room "
                          << room_name << " at (" << spawn.pos.x << "," << spawn.pos.y << ")\n";
                continue;
            }

            NpcRuntime npc{};
            npc.id = state.next_npc_id++;
            npc.def_id = def.id;
            npc.name = def.name;
            npc.sprite_tileset = def.sprite_tileset;
            npc.sprite_name = def.id;
            npc.room = room_name;
            npc.pos = spawn.pos;
            npc.home_pos = spawn.pos;
            npc.size_w = std::max(1, def.npc_size_w);
            npc.size_h = std::max(1, def.npc_size_h);
            npc.facing = Facing::South;
            npc.speed_ms = std::max(1, def.speed_ms);
            npc.move_accum_ms = 0;
            npc.talk_pause_ms = 0;
            npc.wander_radius = std::max(0, def.wander_radius);
            npc.dialogues = def.dialogues;
            state.npcs.push_back(std::move(npc));
        }
    }
}
