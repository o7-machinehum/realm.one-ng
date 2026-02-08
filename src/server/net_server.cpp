#include "net_server.h"

#include "auth_db.h"
#include "monster_defs.h"
#include "world.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <cctype>
#include <unordered_map>
#include <vector>

namespace {

struct MonsterRuntime {
    int id = 0;
    std::string name;
    std::string sprite_tileset;
    std::string sprite_name;
    std::string room;
    int x = 0;
    int y = 0;
    int size_w = 1;
    int size_h = 1;
    int hp = 30;
    int max_hp = 30;
    int speed_ms = 500;
    int move_accum_ms = 0;
    int exp_reward = 10;
};

struct GroundItemRuntime {
    int id = 0;
    std::string name;
    std::string room;
    int x = 0;
    int y = 0;
};

struct PlayerRuntime {
    ENetPeer* peer = nullptr;
    bool authenticated = false;
    PersistedPlayer data;
    int hp = 100;
    int max_hp = 100;
    int attack_target_monster_id = -1;
};

void sendWire(ENetPeer* peer,
              uint8_t channel,
              const std::vector<uint8_t>& wire,
              bool reliable = true) {
    ENetPacket* pkt = enet_packet_create(
        wire.data(),
        wire.size(),
        reliable ? ENET_PACKET_FLAG_RELIABLE : 0
    );
    enet_peer_send(peer, channel, pkt);
}

void printPeerIp(const ENetAddress& a) {
    char ip[64]{};
    enet_address_get_host_ip(&a, ip, sizeof(ip));
    std::cout << ip;
}

int clampStep(int d) {
    if (d > 1) return 1;
    if (d < -1) return -1;
    return d;
}

std::string normalizeId(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

GameStateMsg makeGameState(const PlayerRuntime& self,
                           const std::unordered_map<ENetPeer*, PlayerRuntime>& players,
                           const std::vector<MonsterRuntime>& monsters,
                           const std::vector<GroundItemRuntime>& items,
                           std::string event_text) {
    GameStateMsg gs;
    gs.your_user = self.data.username;
    gs.your_room = self.data.room;
    gs.your_x = self.data.x;
    gs.your_y = self.data.y;
    gs.your_exp = self.data.exp;
    gs.your_hp = self.hp;
    gs.your_max_hp = self.max_hp;
    gs.attack_target_monster_id = self.attack_target_monster_id;
    gs.inventory = self.data.inventory;
    gs.event_text = std::move(event_text);

    for (const auto& [_, p] : players) {
        if (!p.authenticated) continue;
        if (p.data.room != self.data.room) continue;

        PlayerStateMsg ps;
        ps.user = p.data.username;
        ps.room = p.data.room;
        ps.x = p.data.x;
        ps.y = p.data.y;
        ps.exp = p.data.exp;
        ps.hp = p.hp;
        ps.max_hp = p.max_hp;
        gs.players.push_back(std::move(ps));
    }

    for (const auto& m : monsters) {
        if (m.room != self.data.room) continue;
        gs.monsters.push_back(MonsterStateMsg{
            m.id, m.name, m.sprite_tileset, m.sprite_name, m.size_w, m.size_h, m.room, m.x, m.y, m.hp, m.max_hp
        });
    }

    for (const auto& i : items) {
        if (i.room != self.data.room) continue;
        gs.items.push_back(GroundItemStateMsg{i.id, i.name, i.room, i.x, i.y});
    }

    return gs;
}

} // namespace

void NetServer::recvLoop() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    ENetAddress addr{};
    addr.host = ENET_HOST_ANY;
    addr.port = port_;

    ENetHost* server = enet_host_create(&addr, 64, 2, 0, 0);
    if (!server) {
        std::cerr << "[server] failed to create ENet host\n";
        return;
    }

    std::cout << "[server] listening on port " << port_ << "\n";

    std::unordered_map<ENetPeer*, PlayerRuntime> players;
    std::vector<MonsterRuntime> monsters;
    std::vector<GroundItemRuntime> items;
    int next_monster_id = 1;
    int next_item_id = 1;

    auto monster_defs = loadMonsterDefs("game/monsters");
    std::unordered_map<std::string, const MonsterDef*> defs_by_id;
    for (const auto& d : monster_defs) defs_by_id[normalizeId(d.id)] = &d;
    if (defs_by_id.empty()) {
        std::cerr << "[server] warning: no monster defs found in game/monsters\n";
    }

    for (const auto& room_name : world_.roomNames()) {
        const Room* room = world_.getRoom(room_name);
        if (!room) continue;

        for (const auto& spawn : room->monster_spawns()) {
            auto dit = defs_by_id.find(normalizeId(spawn.monster_id));
            if (dit == defs_by_id.end()) {
                std::cerr << "[server] unknown monster id '" << spawn.monster_id
                          << "' in room " << room_name << "\n";
                continue;
            }
            const MonsterDef& def = *dit->second;
            const int sw = std::max(1, def.monster_size_w);
            const int sh = std::max(1, def.monster_size_h);
            if (!room->isWalkable(spawn.x, spawn.y)) {
                std::cerr << "[server] monster spawn '" << spawn.monster_id
                          << "' overlaps blocked anchor tile in room "
                          << room_name << " at (" << spawn.x << "," << spawn.y << ")\n";
                continue;
            }
            bool overlap = false;
            for (const auto& m : monsters) {
                if (m.room != room_name || m.hp <= 0) continue;
                if (m.x == spawn.x && m.y == spawn.y) { overlap = true; break; }
            }
            if (overlap) {
                std::cerr << "[server] overlapping monster spawn in room "
                          << room_name << " at (" << spawn.x << "," << spawn.y << ")\n";
                continue;
            }

            monsters.push_back(MonsterRuntime{
                next_monster_id++,
                spawn.name_override.empty() ? def.name : spawn.name_override,
                def.sprite_tileset,
                def.sprite_name,
                room_name,
                spawn.x,
                spawn.y,
                sw,
                sh,
                def.max_hp,
                def.max_hp,
                std::max(1, def.speed_ms),
                0,
                def.exp_reward
            });
        }
        items.push_back(GroundItemRuntime{next_item_id++, "health_potion", room_name, 3, 3});
        items.push_back(GroundItemRuntime{next_item_id++, "gold_coin", room_name, 6, 4});
    }

    auto sendRoom = [&](ENetPeer* peer, const std::string& room_name) {
        const Room* room = world_.getRoom(room_name);
        if (!room) room = world_.defaultRoom();
        if (room) sendWire(peer, 0, pack(MsgType::Room, *room));
    };

    auto savePlayerNow = [&](const PlayerRuntime& p) {
        if (p.authenticated) auth_db_.savePlayer(p.data);
    };

    auto broadcastState = [&](const std::string& event_text) {
        for (const auto& [peer, p] : players) {
            if (!p.authenticated) continue;
            auto gs = makeGameState(p, players, monsters, items, event_text);
            sendWire(peer, 0, pack(MsgType::GameState, gs));
        }
        enet_host_flush(server);
    };

    auto monsterOccupies = [](const MonsterRuntime& m, int tx, int ty) -> bool {
        if (m.hp <= 0) return false;
        return (tx == m.x && ty == m.y);
    };

    auto occupiedByMonster = [&](const std::string& room_name, int x, int y) -> bool {
        for (const auto& m : monsters) {
            if (m.room != room_name) continue;
            if (monsterOccupies(m, x, y)) return true;
        }
        return false;
    };

    auto occupiedByPlayer = [&](ENetPeer* self, const std::string& room_name, int x, int y) -> bool {
        for (const auto& [peer, p] : players) {
            if (peer == self) continue;
            if (!p.authenticated) continue;
            if (p.data.room == room_name && p.data.x == x && p.data.y == y) return true;
        }
        return false;
    };

    auto canMonsterStand = [&](const MonsterRuntime& mon,
                               int nx,
                               int ny,
                               int ignore_monster_id) -> bool {
        const Room* room = world_.getRoom(mon.room);
        if (!room) return false;
        if (!room->isWalkable(nx, ny)) return false;
        for (const auto& [_, p] : players) {
            if (!p.authenticated) continue;
            if (p.data.room != mon.room) continue;
            if (p.data.x == nx && p.data.y == ny) return false;
        }
        for (const auto& other : monsters) {
            if (other.id == ignore_monster_id) continue;
            if (other.room != mon.room || other.hp <= 0) continue;
            if (other.x == nx && other.y == ny) return false;
        }
        return true;
    };

    const auto tick_dt = std::chrono::milliseconds(500);
    auto last_tick = std::chrono::steady_clock::now();

    while (running_) {
        ENetEvent ev{};
        while (enet_host_service(server, &ev, 40) > 0) {
            if (ev.type == ENET_EVENT_TYPE_CONNECT) {
                std::cout << "[server] connect from ";
                printPeerIp(ev.peer->address);
                std::cout << "\n";
                players[ev.peer] = PlayerRuntime{ev.peer, false, PersistedPlayer{}, 100, 100, -1};
            }

            else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                try {
                    Envelope env = fromBytes<Envelope>(ev.packet->data, ev.packet->dataLength);

                    auto pit = players.find(ev.peer);
                    if (pit == players.end()) {
                        players[ev.peer] = PlayerRuntime{ev.peer, false, PersistedPlayer{}, 100, 100, -1};
                        pit = players.find(ev.peer);
                    }
                    PlayerRuntime& player = pit->second;

                    switch (env.type) {
                        case MsgType::Login: {
                            LoginMsg m = fromBytes<LoginMsg>(env.payload.data(), env.payload.size());

                            PersistedPlayer persisted;
                            std::string message;
                            bool ok = auth_db_.verifyOrCreateUser(m.user, m.pass, persisted, message);

                            LoginResultMsg result;
                            result.ok = ok;
                            result.message = message;
                            result.user = m.user;
                            persisted.room = world_.resolveRoomName(persisted.room, {});
                            if (persisted.room.empty() && world_.defaultRoom()) {
                                persisted.room = world_.roomNames().empty() ? std::string{} : world_.roomNames().front();
                            }
                            result.room = persisted.room;
                            result.exp = persisted.exp;
                            sendWire(ev.peer, 0, pack(MsgType::LoginResult, result));

                            if (ok) {
                                const Room* room = world_.getRoom(persisted.room);
                                if (!room) {
                                    room = world_.defaultRoom();
                                    if (room) persisted.room = world_.resolveRoomName(room->get_name(), {});
                                }
                                if (room) {
                                    persisted.x = std::max(0, std::min(persisted.x, room->map_width() - 1));
                                    persisted.y = std::max(0, std::min(persisted.y, room->map_height() - 1));
                                }

                                player.authenticated = true;
                                player.data = std::move(persisted);
                                player.hp = player.max_hp;
                                player.attack_target_monster_id = -1;

                                std::cout << "[server] LOGIN user=" << player.data.username
                                          << " result=ok room=" << player.data.room << "\n";

                                sendRoom(ev.peer, player.data.room);
                                savePlayerNow(player);
                                broadcastState(player.data.username + " entered " + player.data.room);
                            } else {
                                std::cout << "[server] LOGIN user=" << m.user << " result=failed\n";
                                enet_host_flush(server);
                            }
                        } break;

                        case MsgType::Move: {
                            if (!player.authenticated) break;
                            MoveMsg m = fromBytes<MoveMsg>(env.payload.data(), env.payload.size());

                            const int dx = clampStep(m.dx);
                            const int dy = clampStep(m.dy);

                            const Room* room = world_.getRoom(player.data.room);
                            if (!room) room = world_.defaultRoom();
                            if (!room) break;

                            const int attempted_x = player.data.x + dx;
                            const int attempted_y = player.data.y + dy;

                            std::string next_room = player.data.room;
                            int next_x = player.data.x;
                            int next_y = player.data.y;

                            if (!world_.resolveEdgeTransition(player.data.room, attempted_x, attempted_y, next_room, next_x, next_y)) {
                                break;
                            }

                            const Room* next_room_ptr = world_.getRoom(next_room);
                            if (!next_room_ptr || !next_room_ptr->isWalkable(next_x, next_y)) {
                                break;
                            }
                            if (occupiedByMonster(next_room, next_x, next_y) ||
                                occupiedByPlayer(ev.peer, next_room, next_x, next_y)) {
                                break;
                            }

                            bool changed_room = (next_room != player.data.room);
                            player.data.room = next_room;
                            player.data.x = next_x;
                            player.data.y = next_y;

                            // Portal trigger check (data-authored in Tiled)
                            if (const Room* post_move_room = world_.getRoom(player.data.room)) {
                                const float cx = player.data.x * post_move_room->tile_width() + (post_move_room->tile_width() * 0.5f);
                                const float cy = player.data.y * post_move_room->tile_height() + (post_move_room->tile_height() * 0.5f);

                                for (const auto& portal : post_move_room->portals()) {
                                    const float px2 = portal.x + portal.w;
                                    const float py2 = portal.y + portal.h;
                                    if (!(cx >= portal.x && cx < px2 && cy >= portal.y && cy < py2)) continue;

                                    const std::string dst_room = world_.resolveRoomName(portal.to_room, player.data.room);
                                    const Room* dst = world_.getRoom(dst_room);
                                    if (!dst) break;

                                    const int tx = std::max(0, std::min(portal.to_x, dst->map_width() - 1));
                                    const int ty = std::max(0, std::min(portal.to_y, dst->map_height() - 1));
                                    if (dst->isWalkable(tx, ty)) {
                                        player.data.room = dst_room;
                                        player.data.x = tx;
                                        player.data.y = ty;
                                        changed_room = true;
                                    }
                                    break;
                                }
                            }

                            savePlayerNow(player);

                            if (changed_room) sendRoom(ev.peer, player.data.room);
                            broadcastState(player.data.username + " moved");
                        } break;

                        case MsgType::Attack: {
                            if (!player.authenticated) break;
                            AttackMsg m = fromBytes<AttackMsg>(env.payload.data(), env.payload.size());
                            player.attack_target_monster_id = m.target_monster_id;

                            if (player.attack_target_monster_id >= 0) {
                                broadcastState(player.data.username + " targets monster " + std::to_string(player.attack_target_monster_id));
                            } else {
                                broadcastState(player.data.username + " stops attacking");
                            }
                        } break;

                        case MsgType::Pickup: {
                            if (!player.authenticated) break;
                            PickupMsg m = fromBytes<PickupMsg>(env.payload.data(), env.payload.size());

                            int idx = -1;
                            for (size_t i = 0; i < items.size(); ++i) {
                                const auto& item = items[i];
                                if (item.room != player.data.room) continue;
                                if (item.x != player.data.x || item.y != player.data.y) continue;
                                if (m.item_id != -1 && item.id != m.item_id) continue;
                                idx = static_cast<int>(i);
                                break;
                            }

                            std::string event = player.data.username + " found no item";
                            if (idx >= 0) {
                                player.data.inventory.push_back(items[idx].name);
                                event = player.data.username + " picked up " + items[idx].name;
                                items.erase(items.begin() + idx);
                                savePlayerNow(player);
                            }

                            broadcastState(event);
                        } break;

                        case MsgType::Drop: {
                            if (!player.authenticated) break;
                            DropMsg m = fromBytes<DropMsg>(env.payload.data(), env.payload.size());

                            std::string event = player.data.username + " has nothing to drop";
                            if (m.inventory_index >= 0 &&
                                m.inventory_index < static_cast<int>(player.data.inventory.size())) {
                                const std::string item_name = player.data.inventory[m.inventory_index];
                                player.data.inventory.erase(player.data.inventory.begin() + m.inventory_index);

                                items.push_back(GroundItemRuntime{
                                    next_item_id++,
                                    item_name,
                                    player.data.room,
                                    player.data.x,
                                    player.data.y
                                });

                                event = player.data.username + " dropped " + item_name;
                                savePlayerNow(player);
                            }

                            broadcastState(event);
                        } break;

                        case MsgType::InventorySwap: {
                            if (!player.authenticated) break;
                            InventorySwapMsg m = fromBytes<InventorySwapMsg>(env.payload.data(), env.payload.size());
                            const int n = static_cast<int>(player.data.inventory.size());
                            if (m.from_index >= 0 && m.to_index >= 0 &&
                                m.from_index < n && m.to_index < n &&
                                m.from_index != m.to_index) {
                                std::swap(player.data.inventory[m.from_index], player.data.inventory[m.to_index]);
                                savePlayerNow(player);
                                broadcastState(player.data.username + " rearranged inventory");
                            }
                        } break;

                        case MsgType::Chat: {
                            ChatMsg m = fromBytes<ChatMsg>(env.payload.data(), env.payload.size());
                            ChatMsg reply{"server", "echo: " + m.text};
                            sendWire(ev.peer, 0, pack(MsgType::Chat, reply));
                            enet_host_flush(server);
                        } break;

                        default:
                            break;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[server] decode error: " << e.what() << "\n";
                }

                enet_packet_destroy(ev.packet);
            }

            else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
                auto it = players.find(ev.peer);
                if (it != players.end()) {
                    if (it->second.authenticated) {
                        savePlayerNow(it->second);
                        std::cout << "[server] disconnect user=" << it->second.data.username << "\n";
                    } else {
                        std::cout << "[server] disconnect\n";
                    }
                    players.erase(it);
                    broadcastState("A player disconnected");
                }
            }
        }

        const auto now = std::chrono::steady_clock::now();
        while (now - last_tick >= tick_dt) {
            last_tick += tick_dt;

            std::string tick_event;
            bool changed = false;

            for (auto& [_, p] : players) {
                if (!p.authenticated) continue;
                if (p.attack_target_monster_id < 0) continue;

                int hit_index = -1;
                for (size_t i = 0; i < monsters.size(); ++i) {
                    if (monsters[i].id == p.attack_target_monster_id) {
                        hit_index = static_cast<int>(i);
                        break;
                    }
                }

                if (hit_index < 0) {
                    p.attack_target_monster_id = -1;
                    changed = true;
                    tick_event = p.data.username + " lost target";
                    continue;
                }

                auto& mon = monsters[hit_index];
                if (mon.room != p.data.room) {
                    continue;
                }

                const int dist = std::abs(mon.x - p.data.x) + std::abs(mon.y - p.data.y);
                if (dist > 1) {
                    continue;
                }

                const int dmg = 6;
                mon.hp = std::max(0, mon.hp - dmg);
                changed = true;
                tick_event = p.data.username + " hits " + mon.name + " for " + std::to_string(dmg);

                if (mon.hp <= 0) {
                    p.data.exp += mon.exp_reward;
                    p.attack_target_monster_id = -1;
                    items.push_back(GroundItemRuntime{next_item_id++, "monster_loot", mon.room, mon.x, mon.y});
                    tick_event = p.data.username + " killed " + mon.name + " (exp +" + std::to_string(mon.exp_reward) + ")";
                    monsters.erase(monsters.begin() + hit_index);
                    savePlayerNow(p);
                }
            }

            for (auto& mon : monsters) {
                if (mon.hp <= 0) continue;
                mon.move_accum_ms += static_cast<int>(tick_dt.count());
                if (mon.move_accum_ms < std::max(1, mon.speed_ms)) continue;
                mon.move_accum_ms = 0;

                // 25% chance to idle this tick to avoid jittery movement.
                if ((std::rand() % 4) == 0) continue;

                static constexpr int dirs[4][2] = {
                    {0, -1}, {1, 0}, {0, 1}, {-1, 0}
                };
                const int start = std::rand() % 4;
                for (int i = 0; i < 4; ++i) {
                    const int* d = dirs[(start + i) % 4];
                    const int nx = mon.x + d[0];
                    const int ny = mon.y + d[1];
                    if (!canMonsterStand(mon, nx, ny, mon.id)) continue;
                    mon.x = nx;
                    mon.y = ny;
                    changed = true;
                    if (tick_event.empty()) tick_event = "monsters moved";
                    break;
                }
            }

            if (changed) {
                broadcastState(tick_event.empty() ? "combat tick" : tick_event);
            }
        }
    }

    for (const auto& [_, p] : players) savePlayerNow(p);
    enet_host_destroy(server);
}
