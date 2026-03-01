#include "server_msg_handlers.h"

#include "server_constants.h"
#include "server_occupancy.h"
#include "server_spawning.h"
#include "server_util.h"
#include "string_util.h"
#include "world.h"

#include <algorithm>
#include <cctype>
#include <iostream>

namespace {

// ---- Individual handlers (file-internal) ----

void handleLoginMessage(ServerState& state, ENetPeer* peer, const Envelope& env) {
    LoginMsg m = fromBytes<LoginMsg>(env.payload.data(), env.payload.size());

    auto pit = state.players.find(peer);
    if (pit == state.players.end()) return;
    PlayerRuntime& player = pit->second;

    PersistedPlayer persisted;
    std::string message;
    const bool ok = authenticateLoginRequest(m, *state.auth_db, persisted, message);

    LoginResultMsg result;
    result.ok = ok;
    result.message = message;
    result.user = m.user;
    persisted.room = state.world->resolveRoomName(persisted.room, {});
    if (persisted.room.empty() && state.world->defaultRoom()) {
        persisted.room = state.world->roomNames().empty() ? std::string{} : state.world->roomNames().front();
    }
    result.room = persisted.room;
    result.exp = persisted.exp;
    sendPacketToPeer(peer, 0, pack(MsgType::LoginResult, result));

    if (ok) {
        const Room* room = state.world->getRoom(persisted.room);
        if (!room) {
            room = state.world->defaultRoom();
            if (room) persisted.room = state.world->resolveRoomName(room->get_name(), {});
        }
        if (room) {
            persisted.pos.x = std::clamp(persisted.pos.x, 0, room->map_width() - 1);
            persisted.pos.y = std::clamp(persisted.pos.y, 0, room->map_height() - 1);
        }

        player.authenticated = true;
        player.data = std::move(persisted);
        pruneEquipmentForInventory(player);
        updatePlayerVitalsFromLevel(player, state);
        player.hp = player.max_hp;
        player.mana = player.max_mana;
        player.combat_outcome = CombatOutcome::None;
        player.combat_outcome_seq = 0;
        player.xp_gain_seq = 0;
        player.xp_gain_amount = 0;
        player.attack_target_monster_id = -1;

        std::cout << "[server] LOGIN user=" << player.data.username
                  << " result=ok room=" << player.data.room << "\n";

        sendRoomToPeer(peer, *state.world, player.data.room);
        persistPlayer(player, *state.auth_db);
        broadcastGameState(state, player.data.username + " entered " + player.data.room);
    } else {
        std::cout << "[server] LOGIN user=" << m.user << " result=failed\n";
        enet_host_flush(state.host);
    }
}

void handleMoveMessage(ServerState& state, ENetPeer* peer, const Envelope& env) {
    auto pit = state.players.find(peer);
    if (pit == state.players.end()) return;
    PlayerRuntime& player = pit->second;
    if (!player.authenticated) return;

    MoveMsg m = fromBytes<MoveMsg>(env.payload.data(), env.payload.size());
    const int dx = clampToSingleStep(m.dx);
    const int dy = clampToSingleStep(m.dy);
    if (dx != 0 || dy != 0) {
        player.facing = facingFromDelta(dx, dy, player.facing);
    }

    const Room* room = state.world->getRoom(player.data.room);
    if (!room) room = state.world->defaultRoom();
    if (!room) return;

    const TilePos attempted = player.data.pos.offset(dx, dy);

    std::string next_room = player.data.room;
    TilePos next_pos = player.data.pos;

    if (!state.world->resolveEdgeTransition(player.data.room, attempted,
                                            next_room, next_pos)) {
        return;
    }

    const Room* next_room_ptr = state.world->getRoom(next_room);
    if (!next_room_ptr || !next_room_ptr->isWalkable(next_pos.x, next_pos.y)) return;
    if (isTileOccupiedByMonster(state, next_room, next_pos) ||
        isTileOccupiedByNpc(state, next_room, next_pos) ||
        isTileOccupiedByPlayer(state, peer, next_room, next_pos)) {
        return;
    }

    bool changed_room = (next_room != player.data.room);
    player.data.room = next_room;
    player.data.pos = next_pos;

    // Portal trigger check (data-authored in Tiled)
    if (const Room* post_move_room = state.world->getRoom(player.data.room)) {
        const float cx = player.data.pos.x * post_move_room->tile_width() + (post_move_room->tile_width() * 0.5f);
        const float cy = player.data.pos.y * post_move_room->tile_height() + (post_move_room->tile_height() * 0.5f);

        for (const auto& portal : post_move_room->portals()) {
            const float px2 = portal.x + portal.w;
            const float py2 = portal.y + portal.h;
            if (!(cx >= portal.x && cx < px2 && cy >= portal.y && cy < py2)) continue;

            const std::string dst_room = state.world->resolveRoomName(portal.to_room, player.data.room);
            const Room* dst = state.world->getRoom(dst_room);
            if (!dst) break;

            const int tx = std::clamp(portal.to_pos.x, 0, dst->map_width() - 1);
            const int ty = std::clamp(portal.to_pos.y, 0, dst->map_height() - 1);
            if (dst->isWalkable(tx, ty)) {
                player.data.room = dst_room;
                player.data.pos = {tx, ty};
                changed_room = true;
            }
            break;
        }
    }

    persistPlayer(player, *state.auth_db);
    if (changed_room) sendRoomToPeer(peer, *state.world, player.data.room);
    broadcastGameState(state, player.data.username + " moved");
}

void handleRotateMessage(ServerState& state, ENetPeer* peer, const Envelope& env) {
    auto pit = state.players.find(peer);
    if (pit == state.players.end()) return;
    PlayerRuntime& player = pit->second;
    if (!player.authenticated) return;

    RotateMsg m = fromBytes<RotateMsg>(env.payload.data(), env.payload.size());
    const int dx = clampToSingleStep(m.dx);
    const int dy = clampToSingleStep(m.dy);
    if (dx == 0 && dy == 0) return;
    player.facing = facingFromDelta(dx, dy, player.facing);
    broadcastGameState(state, player.data.username + " turns");
}

void handleAttackMessage(ServerState& state, ENetPeer* peer, const Envelope& env) {
    auto pit = state.players.find(peer);
    if (pit == state.players.end()) return;
    PlayerRuntime& player = pit->second;
    if (!player.authenticated) return;

    AttackMsg m = fromBytes<AttackMsg>(env.payload.data(), env.payload.size());
    player.attack_target_monster_id = m.target_monster_id;
    if (player.attack_target_monster_id >= 0) {
        for (const auto& mon : state.monsters) {
            if (mon.id != player.attack_target_monster_id) continue;
            if (mon.room != player.data.room) break;
            player.facing = facingFromDelta(mon.pos.x - player.data.pos.x, mon.pos.y - player.data.pos.y, player.facing);
            break;
        }
    }

    if (player.attack_target_monster_id >= 0) {
        broadcastGameState(state, player.data.username + " targets monster " + std::to_string(player.attack_target_monster_id));
    } else {
        broadcastGameState(state, player.data.username + " stops attacking");
    }
}

void handlePickupMessage(ServerState& state, ENetPeer* peer, const Envelope& env) {
    auto pit = state.players.find(peer);
    if (pit == state.players.end()) return;
    PlayerRuntime& player = pit->second;
    if (!player.authenticated) return;

    PickupMsg m = fromBytes<PickupMsg>(env.payload.data(), env.payload.size());
    const int idx = findPickupCandidateIndex(state, player, m.item_id);

    std::string event = player.data.username + " found no item";
    if (idx >= 0) {
        if (static_cast<int>(player.data.inventory.size()) >= kInventoryLimit) {
            event = player.data.username + " inventory is full";
            broadcastGameState(state, event);
            return;
        }
        if (parseCorpseMonsterId(state.items[idx].item_id).empty()) {
            player.data.inventory.push_back(state.items[idx].name);
        } else {
            player.data.inventory.push_back(state.items[idx].item_id);
        }
        event = player.data.username + " picked up " + state.items[idx].name;
        state.items.erase(state.items.begin() + idx);
        persistPlayer(player, *state.auth_db);
    }

    broadcastGameState(state, event);
}

void handleDropMessage(ServerState& state, ENetPeer* peer, const Envelope& env) {
    auto pit = state.players.find(peer);
    if (pit == state.players.end()) return;
    PlayerRuntime& player = pit->second;
    if (!player.authenticated) return;

    DropMsg m = fromBytes<DropMsg>(env.payload.data(), env.payload.size());
    std::string event = player.data.username + " has nothing to drop";
    if (m.inventory_index >= 0 &&
        m.inventory_index < static_cast<int>(player.data.inventory.size())) {
        const Room* room = state.world->getRoom(player.data.room);
        if (!room) return;

        TilePos drop_pos = player.data.pos;
        if (m.to_x >= 0 && m.to_y >= 0) {
            drop_pos = {m.to_x, m.to_y};
        }
        if (drop_pos.x < 0 || drop_pos.y < 0 ||
            drop_pos.x >= room->map_width() || drop_pos.y >= room->map_height()) {
            return;
        }
        if (!room->isWalkable(drop_pos.x, drop_pos.y)) return;
        if (isTileOccupiedByMonster(state, player.data.room, drop_pos) ||
            isTileOccupiedByNpc(state, player.data.room, drop_pos) ||
            isTileOccupiedByPlayer(state, peer, player.data.room, drop_pos)) {
            return;
        }
        const int throw_dist = tileDistance(player.data.pos, drop_pos);
        if (throw_dist > kThrowRangeTiles) return;

        const std::string inv_entry = player.data.inventory[m.inventory_index];
        GroundItemRuntime drop_item{};
        drop_item.id = state.next_item_id++;
        drop_item.room = player.data.room;
        drop_item.pos = drop_pos;

        const std::string corpse_monster_id = parseCorpseMonsterId(inv_entry);
        bool can_drop = false;
        if (!corpse_monster_id.empty()) {
            auto dit = state.monster_defs_by_id.find(corpse_monster_id);
            if (dit != state.monster_defs_by_id.end()) {
                const MonsterDef& def = *dit->second;
                drop_item.item_id = makeCorpseItemId(def.id);
                drop_item.name = def.name + " corpse";
                drop_item.sprite_tileset = def.sprite_tileset;
                drop_item.sprite_name = def.id;
                drop_item.sprite_w_tiles = std::max(1, def.monster_size_w);
                drop_item.sprite_h_tiles = std::max(1, def.monster_size_h);
                drop_item.sprite_clip = 1;
                can_drop = true;
            }
        } else {
            const std::string item_name = inv_entry;
            std::string item_id = normalizeId(item_name);
            drop_item.item_id = item_id;
            drop_item.name = item_name;
            drop_item.sprite_tileset = "materials2.tsx";
            drop_item.sprite_name = item_id;
            auto def_it = state.item_defs_by_id.find(item_id);
            if (def_it != state.item_defs_by_id.end()) {
                drop_item.item_id = def_it->second->id;
                drop_item.name = item_name;
                drop_item.sprite_tileset = def_it->second->sprite_tileset;
                drop_item.sprite_name = def_it->second->id;
            }
            can_drop = true;
        }
        if (!can_drop) return;

        player.data.inventory.erase(player.data.inventory.begin() + m.inventory_index);
        onInventoryErase(player, m.inventory_index);
        state.items.push_back(std::move(drop_item));

        event = player.data.username + " dropped " + state.items.back().name;
        persistPlayer(player, *state.auth_db);
    }

    broadcastGameState(state, event);
}

void handleInventorySwapMessage(ServerState& state, ENetPeer* peer, const Envelope& env) {
    auto pit = state.players.find(peer);
    if (pit == state.players.end()) return;
    PlayerRuntime& player = pit->second;
    if (!player.authenticated) return;

    InventorySwapMsg m = fromBytes<InventorySwapMsg>(env.payload.data(), env.payload.size());
    const int n = static_cast<int>(player.data.inventory.size());
    if (m.from_index >= 0 && m.to_index >= 0 &&
        m.from_index < n && m.to_index < n &&
        m.from_index != m.to_index) {
        std::swap(player.data.inventory[m.from_index], player.data.inventory[m.to_index]);
        onInventorySwap(player, m.from_index, m.to_index);
        persistPlayer(player, *state.auth_db);
        broadcastGameState(state, player.data.username + " rearranged inventory");
    }
}

void handleSetEquipmentMessage(ServerState& state, ENetPeer* peer, const Envelope& env) {
    auto pit = state.players.find(peer);
    if (pit == state.players.end()) return;
    PlayerRuntime& player = pit->second;
    if (!player.authenticated) return;

    SetEquipmentMsg m = fromBytes<SetEquipmentMsg>(env.payload.data(), env.payload.size());
    const std::string equip_type = canonicalEquipType(m.equip_type);
    if (equip_type.empty()) return;

    if (m.inventory_index < 0) {
        player.data.equipment_by_type.erase(equip_type);
        persistPlayer(player, *state.auth_db);
        broadcastGameState(state, player.data.username + " unequipped " + equip_type);
        return;
    }

    if (m.inventory_index >= static_cast<int>(player.data.inventory.size())) return;
    const std::string& inv_item_name = player.data.inventory[m.inventory_index];
    auto it = state.item_defs_by_id.find(normalizeId(inv_item_name));
    if (it == state.item_defs_by_id.end()) return;
    const std::string item_type = canonicalEquipType(it->second->item_type);
    if (item_type.empty() || item_type != equip_type) return;

    player.data.equipment_by_type[equip_type] = m.inventory_index;
    persistPlayer(player, *state.auth_db);
    broadcastGameState(state, player.data.username + " equipped " + inv_item_name);
}

void handleMoveGroundItemMessage(ServerState& state, ENetPeer* peer, const Envelope& env) {
    auto pit = state.players.find(peer);
    if (pit == state.players.end()) return;
    PlayerRuntime& player = pit->second;
    if (!player.authenticated) return;

    MoveGroundItemMsg m = fromBytes<MoveGroundItemMsg>(env.payload.data(), env.payload.size());
    const Room* room = state.world->getRoom(player.data.room);
    if (!room) return;

    const TilePos target{m.to_x, m.to_y};
    if (target.x < 0 || target.y < 0 || target.x >= room->map_width() || target.y >= room->map_height()) return;
    if (!room->isWalkable(target.x, target.y)) return;
    if (isTileOccupiedByMonster(state, player.data.room, target) ||
        isTileOccupiedByNpc(state, player.data.room, target) ||
        isTileOccupiedByPlayer(state, peer, player.data.room, target)) {
        return;
    }

    const int idx = findGroundItemIndexById(state, player, m.item_id);
    if (idx < 0) return;
    if (!isItemReachableByPlayer(player, state.items[static_cast<size_t>(idx)])) return;

    const int throw_dist = tileDistance(target, player.data.pos);
    if (throw_dist > kThrowRangeTiles) return;

    state.items[idx].pos = target;
    broadcastGameState(state, player.data.username + " moved " + state.items[idx].name);
}

void handleChatMessage(ServerState& state, ENetPeer* peer, const Envelope& env) {
    auto pit = state.players.find(peer);
    if (pit == state.players.end()) return;
    PlayerRuntime& player = pit->second;
    if (!player.authenticated) return;

    ChatMsg m = fromBytes<ChatMsg>(env.payload.data(), env.payload.size());
    std::string text = trimWhitespace(m.text);
    if (text.empty()) return;
    if (text.size() > 120) text.resize(120);
    std::string speech_type = normalizeId(m.speech_type);
    if (speech_type != "talk" && speech_type != "think" && speech_type != "yell") {
        speech_type = "talk";
    }

    ChatMsg out{player.data.username, speech_type, text};
    for (const auto& [p_peer, p] : state.players) {
        if (!p.authenticated) continue;
        if (p.data.room != player.data.room) continue;
        sendPacketToPeer(p_peer, 0, pack(MsgType::Chat, out));
    }

    // ---- NPC dialogue matching ----
    auto normalizeText = [](std::string s) -> std::string {
        s = toLowerAscii(std::move(s));
        for (char& c : s) {
            if (!(std::isalnum(static_cast<unsigned char>(c)) || std::isspace(static_cast<unsigned char>(c)))) {
                c = ' ';
            }
        }
        return trimWhitespace(std::move(s));
    };

    const std::string norm_text = normalizeText(text);
    auto containsTopic = [](const std::string& hay, const std::string& needle) -> bool {
        if (hay.empty() || needle.empty()) return false;
        if (hay == needle) return true;
        const std::string padded_hay = " " + hay + " ";
        const std::string padded_needle = " " + needle + " ";
        return padded_hay.find(padded_needle) != std::string::npos;
    };

    int best_idx = -1;
    int best_dist = 999999;
    std::string npc_reply;
    std::string npc_name;
    int speaker_dx = 0;
    int speaker_dy = 0;
    for (size_t i = 0; i < state.npcs.size(); ++i) {
        const auto& npc = state.npcs[i];
        if (npc.room != player.data.room) continue;
        const int dist = tileDistance(player.data.pos, npc.pos);
        if (dist > kNpcTalkRangeTiles) continue;
        std::string matched_reply;
        for (const auto& d : npc.dialogues) {
            if (d.response.empty()) continue;
            for (const auto& q : d.questions) {
                const std::string nq = normalizeText(q);
                if (nq.empty()) continue;
                if (containsTopic(norm_text, nq)) {
                    matched_reply = d.response;
                    break;
                }
            }
            if (!matched_reply.empty()) break;
        }
        if (matched_reply.empty()) continue;
        if (dist >= best_dist) continue;
        best_dist = dist;
        best_idx = static_cast<int>(i);
        npc_reply = matched_reply;
        npc_name = npc.name;
        speaker_dx = player.data.pos.x - npc.pos.x;
        speaker_dy = player.data.pos.y - npc.pos.y;
    }
    if (best_idx >= 0 && !npc_reply.empty() && !npc_name.empty()) {
        state.npcs[static_cast<size_t>(best_idx)].facing = facingFromDelta(
            speaker_dx, speaker_dy,
            state.npcs[static_cast<size_t>(best_idx)].facing);
        state.npcs[static_cast<size_t>(best_idx)].talk_pause_ms =
            std::max(state.npcs[static_cast<size_t>(best_idx)].talk_pause_ms, 3000);
        ChatMsg npc_chat{npc_name, "talk", npc_reply};
        for (const auto& [p_peer, p] : state.players) {
            if (!p.authenticated) continue;
            if (p.data.room != player.data.room) continue;
            sendPacketToPeer(p_peer, 0, pack(MsgType::Chat, npc_chat));
        }
    }
    enet_host_flush(state.host);
}

} // namespace

// ---- Public dispatch ----

void dispatchMessage(ServerState& state, ENetPeer* peer, const Envelope& env) {
    switch (env.type) {
        case MsgType::Login:          handleLoginMessage(state, peer, env);          break;
        case MsgType::Move:           handleMoveMessage(state, peer, env);           break;
        case MsgType::Rotate:         handleRotateMessage(state, peer, env);         break;
        case MsgType::Attack:         handleAttackMessage(state, peer, env);         break;
        case MsgType::Pickup:         handlePickupMessage(state, peer, env);         break;
        case MsgType::Drop:           handleDropMessage(state, peer, env);           break;
        case MsgType::InventorySwap:  handleInventorySwapMessage(state, peer, env);  break;
        case MsgType::SetEquipment:   handleSetEquipmentMessage(state, peer, env);   break;
        case MsgType::MoveGroundItem: handleMoveGroundItemMessage(state, peer, env); break;
        case MsgType::Chat:           handleChatMessage(state, peer, env);           break;
        default: break;
    }
}
