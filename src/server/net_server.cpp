#include "net_server.h"

#include "auth_db.h"
#include "auth_crypto.h"
#include "item_defs.h"
#include "monster_combat.h"
#include "monster_defs.h"
#include "npc_defs.h"
#include "server_runtime.h"
#include "world.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <cctype>
#include <unordered_map>
#include <vector>

namespace {
constexpr int kDefaultFacingSouth = 2;
constexpr int kMaxMoveStep = 1;
constexpr int kMeleeRangeTiles = 1;
constexpr int kThrowRangeTiles = 5;
constexpr int kInventoryLimit = 8;
constexpr int kCombatDamagePerTick = 6;
constexpr int kTickMs = 500;
constexpr int kMonsterIdleChanceDivisor = 4; // 1/N idle chance per tick
constexpr int kNpcTalkRangeTiles = 8;

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
    if (d > kMaxMoveStep) return kMaxMoveStep;
    if (d < -kMaxMoveStep) return -kMaxMoveStep;
    return d;
}

int facingFromDelta(int dx, int dy, int fallback = kDefaultFacingSouth) {
    if (dx > 0) return 1;
    if (dx < 0) return 3;
    if (dy > 0) return 2;
    if (dy < 0) return 0;
    return fallback;
}

int signum(int v) {
    if (v > 0) return 1;
    if (v < 0) return -1;
    return 0;
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

constexpr const char* kCorpsePrefix = "corpse:";

std::string makeCorpseItemId(const std::string& monster_id) {
    return std::string(kCorpsePrefix) + normalizeId(monster_id);
}

std::string parseCorpseMonsterId(const std::string& item_id) {
    const std::string n = normalizeId(item_id);
    const std::string prefix = kCorpsePrefix;
    if (n.rfind(prefix, 0) != 0) return {};
    return n.substr(prefix.size());
}

std::vector<EquippedItemMsg> toEquippedMsgVec(const std::unordered_map<std::string, int>& eq,
                                              const std::vector<std::string>& inventory) {
    std::vector<EquippedItemMsg> out;
    out.reserve(eq.size());
    for (const auto& [t, idx] : eq) {
        if (t.empty() || idx < 0 || idx >= static_cast<int>(inventory.size())) continue;
        out.push_back(EquippedItemMsg{t, idx, inventory[(size_t)idx]});
    }
    return out;
}

GameStateMsg makeGameState(const PlayerRuntime& self,
                           const std::unordered_map<ENetPeer*, PlayerRuntime>& players,
                           const std::vector<MonsterRuntime>& monsters,
                           const std::vector<NpcRuntime>& npcs,
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
    gs.your_mana = self.mana;
    gs.your_max_mana = self.max_mana;
    gs.your_equipment = toEquippedMsgVec(self.data.equipment_by_type, self.data.inventory);
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
        ps.mana = p.mana;
        ps.max_mana = p.max_mana;
        ps.facing = p.facing;
        ps.attack_anim_seq = p.attack_anim_seq;
        ps.equipment = toEquippedMsgVec(p.data.equipment_by_type, p.data.inventory);
        gs.players.push_back(std::move(ps));
    }

    for (const auto& m : monsters) {
        if (m.room != self.data.room) continue;
        gs.monsters.push_back(MonsterStateMsg{
            m.id, m.name, m.sprite_tileset, m.sprite_name, m.size_w, m.size_h, m.room, m.x, m.y, m.hp, m.max_hp, m.facing, m.attack_anim_seq
        });
    }

    for (const auto& n : npcs) {
        if (n.room != self.data.room) continue;
        gs.npcs.push_back(NpcStateMsg{
            n.id, n.name, n.sprite_tileset, n.sprite_name, n.size_w, n.size_h, n.room, n.x, n.y, n.facing
        });
    }

    for (const auto& i : items) {
        if (i.room != self.data.room) continue;
        gs.items.push_back(GroundItemStateMsg{
            i.id, i.name, i.sprite_tileset, i.sprite_name,
            i.sprite_w_tiles, i.sprite_h_tiles, i.sprite_clip,
            i.room, i.x, i.y
        });
    }

    return gs;
}

// Validates login signature and executes create/login against persistent auth storage.
bool authenticateLoginRequest(const LoginMsg& m,
                              AuthDb& auth_db,
                              PersistedPlayer& persisted,
                              std::string& message) {
    if (m.user.empty() || m.public_key_hex.empty() || m.signature_hex.empty()) {
        message = "missing login fields";
        return false;
    }
    const std::string payload = makeAuthPayload(m.user, m.public_key_hex, m.create_account);
    if (!verifyEd25519Hex(m.public_key_hex, payload, m.signature_hex)) {
        message = "invalid signature";
        return false;
    }
    return auth_db.loginWithPublicKey(
        m.user,
        m.public_key_hex,
        m.create_account,
        persisted,
        message
    );
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
    std::vector<NpcRuntime> npcs;
    std::vector<GroundItemRuntime> items;
    int next_monster_id = 1;
    int next_npc_id = 1;
    int next_item_id = 1;

    auto monster_defs = loadMonsterDefs("game/monsters");
    std::unordered_map<std::string, const MonsterDef*> defs_by_id;
    for (const auto& d : monster_defs) defs_by_id[normalizeId(d.id)] = &d;
    if (defs_by_id.empty()) {
        std::cerr << "[server] warning: no monster defs found in game/monsters\n";
    }
    auto npc_defs = loadNpcDefs("game/npcs");
    std::unordered_map<std::string, const NpcDef*> npc_defs_by_id;
    for (const auto& d : npc_defs) npc_defs_by_id[normalizeId(d.id)] = &d;
    if (npc_defs_by_id.empty()) {
        std::cerr << "[server] warning: no npc defs found in game/npcs\n";
    }
    auto item_defs = loadItemDefs("game/items");
    std::unordered_map<std::string, const ItemDef*> item_defs_by_id;
    for (const auto& d : item_defs) {
        item_defs_by_id[normalizeId(d.id)] = &d;
        item_defs_by_id[normalizeId(d.name)] = &d;
    }
    if (item_defs_by_id.empty()) {
        std::cerr << "[server] warning: no item defs found in game/items\n";
    }

    auto spawnItemById = [&](const std::string& raw_item_id,
                             const std::string& room_name,
                             int x,
                             int y) {
        const std::string item_id = normalizeId(raw_item_id);
        auto dit = item_defs_by_id.find(item_id);
        if (dit == item_defs_by_id.end()) {
            std::cerr << "[server] unknown item id '" << raw_item_id
                      << "' in room " << room_name << "\n";
            return;
        }
        const ItemDef& def = *dit->second;
        items.push_back(GroundItemRuntime{
            next_item_id++,
            def.id,
            def.name,
            def.sprite_tileset,
            def.id,
            1,
            1,
            0,
            room_name,
            x,
            y
        });
    };

    auto canonicalEquipType = [&](const std::string& raw) -> std::string {
        const std::string n = normalizeId(raw);
        if (n == "weapon") return "Weapon";
        if (n == "armor") return "Armor";
        if (n == "shield") return "Shield";
        if (n == "legs") return "Legs";
        if (n == "boots") return "Boots";
        if (n == "helmet") return "Helmet";
        return {};
    };

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
                def.id,
                spawn.name_override.empty() ? def.name : spawn.name_override,
                def.sprite_tileset,
                def.id,
                room_name,
                spawn.x,
                spawn.y,
                sw,
                sh,
                def.max_hp,
                def.max_hp,
                std::max(1, def.strength),
                kDefaultFacingSouth,
                0,
                std::max(1, def.speed_ms),
                0,
                def.exp_reward,
                def.drops
            });
        }
        for (const auto& spawn : room->item_spawns()) {
            if (!room->isWalkable(spawn.x, spawn.y)) {
                std::cerr << "[server] item spawn '" << spawn.item_id
                          << "' overlaps blocked tile in room "
                          << room_name << " at (" << spawn.x << "," << spawn.y << ")\n";
                continue;
            }
            spawnItemById(spawn.item_id, room_name, spawn.x, spawn.y);
        }
        for (const auto& spawn : room->npc_spawns()) {
            auto dit = npc_defs_by_id.find(normalizeId(spawn.npc_id));
            if (dit == npc_defs_by_id.end()) {
                std::cerr << "[server] unknown npc id '" << spawn.npc_id
                          << "' in room " << room_name << "\n";
                continue;
            }
            const NpcDef& def = *dit->second;
            if (!room->isWalkable(spawn.x, spawn.y)) {
                std::cerr << "[server] npc spawn '" << spawn.npc_id
                          << "' overlaps blocked anchor tile in room "
                          << room_name << " at (" << spawn.x << "," << spawn.y << ")\n";
                continue;
            }
            bool overlap = false;
            for (const auto& m : monsters) {
                if (m.room != room_name || m.hp <= 0) continue;
                if (m.x == spawn.x && m.y == spawn.y) { overlap = true; break; }
            }
            if (!overlap) {
                for (const auto& n : npcs) {
                    if (n.room != room_name) continue;
                    if (n.x == spawn.x && n.y == spawn.y) { overlap = true; break; }
                }
            }
            if (overlap) {
                std::cerr << "[server] overlapping npc spawn in room "
                          << room_name << " at (" << spawn.x << "," << spawn.y << ")\n";
                continue;
            }
            npcs.push_back(NpcRuntime{
                next_npc_id++,
                def.id,
                def.name,
                def.sprite_tileset,
                def.id,
                room_name,
                spawn.x,
                spawn.y,
                spawn.x,
                spawn.y,
                std::max(1, def.npc_size_w),
                std::max(1, def.npc_size_h),
                kDefaultFacingSouth,
                std::max(1, def.speed_ms),
                0,
                0,
                std::max(0, def.wander_radius),
                def.dialogues
            });
        }
    }

    auto sendRoom = [&](ENetPeer* peer, const std::string& room_name) {
        const Room* room = world_.getRoom(room_name);
        if (!room) room = world_.defaultRoom();
        if (room) sendWire(peer, 0, pack(MsgType::Room, *room));
    };

    auto savePlayerNow = [&](const PlayerRuntime& p) {
        if (p.authenticated) auth_db_.savePlayer(p.data);
    };

    // Pushes the authoritative room-local snapshot to every authenticated client.
    auto broadcastState = [&](const std::string& event_text) {
        for (const auto& [peer, p] : players) {
            if (!p.authenticated) continue;
            auto gs = makeGameState(p, players, monsters, npcs, items, event_text);
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

    auto occupiedByNpc = [&](const std::string& room_name, int x, int y) -> bool {
        for (const auto& n : npcs) {
            if (n.room != room_name) continue;
            if (n.x == x && n.y == y) return true;
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

    auto tileDistance = [](int ax, int ay, int bx, int by) -> int {
        return std::abs(ax - bx) + std::abs(ay - by);
    };

    auto pruneEquipmentForInventory = [&](PlayerRuntime& p) {
        for (auto it = p.data.equipment_by_type.begin(); it != p.data.equipment_by_type.end();) {
            if (it->second < 0 || it->second >= static_cast<int>(p.data.inventory.size())) {
                it = p.data.equipment_by_type.erase(it);
            } else {
                ++it;
            }
        }
    };

    auto onInventoryErase = [&](PlayerRuntime& p, int erased_index) {
        for (auto it = p.data.equipment_by_type.begin(); it != p.data.equipment_by_type.end();) {
            if (it->second == erased_index) {
                it = p.data.equipment_by_type.erase(it);
            } else {
                if (it->second > erased_index) it->second -= 1;
                ++it;
            }
        }
    };

    auto onInventorySwap = [&](PlayerRuntime& p, int a, int b) {
        for (auto& [_, idx] : p.data.equipment_by_type) {
            if (idx == a) idx = b;
            else if (idx == b) idx = a;
        }
    };

    auto isItemReachableByPlayer = [&](const PlayerRuntime& p, const GroundItemRuntime& item) -> bool {
        if (item.room != p.data.room) return false;
        const int dx = std::abs(p.data.x - item.x);
        const int dy = std::abs(p.data.y - item.y);
        return std::max(dx, dy) <= kMeleeRangeTiles;
    };

    auto findGroundItemIndexById = [&](const PlayerRuntime& p, int item_id) -> int {
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].id != item_id) continue;
            if (items[i].room != p.data.room) continue;
            return static_cast<int>(i);
        }
        return -1;
    };

    auto findPickupCandidateIndex = [&](const PlayerRuntime& p, int requested_item_id) -> int {
        if (requested_item_id != -1) {
            const int idx = findGroundItemIndexById(p, requested_item_id);
            if (idx < 0) return -1;
            return isItemReachableByPlayer(p, items[(size_t)idx]) ? idx : -1;
        }

        int best_idx = -1;
        int best_dist = 999999;
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            if (!isItemReachableByPlayer(p, item)) continue;
            const int dist = tileDistance(p.data.x, p.data.y, item.x, item.y);
            if (dist < best_dist) {
                best_dist = dist;
                best_idx = static_cast<int>(i);
            }
        }
        return best_idx;
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
        for (const auto& n : npcs) {
            if (n.room != mon.room) continue;
            if (n.x == nx && n.y == ny) return false;
        }
        for (const auto& other : monsters) {
            if (other.id == ignore_monster_id) continue;
            if (other.room != mon.room || other.hp <= 0) continue;
            if (other.x == nx && other.y == ny) return false;
        }
        return true;
    };

    auto canNpcStand = [&](const NpcRuntime& npc,
                           int nx,
                           int ny,
                           int ignore_npc_id) -> bool {
        const Room* room = world_.getRoom(npc.room);
        if (!room) return false;
        if (!room->isWalkable(nx, ny)) return false;
        if (std::abs(nx - npc.home_x) + std::abs(ny - npc.home_y) > std::max(0, npc.wander_radius)) return false;
        for (const auto& [_, p] : players) {
            if (!p.authenticated) continue;
            if (p.data.room != npc.room) continue;
            if (p.data.x == nx && p.data.y == ny) return false;
        }
        for (const auto& m : monsters) {
            if (m.room != npc.room || m.hp <= 0) continue;
            if (m.x == nx && m.y == ny) return false;
        }
        for (const auto& other : npcs) {
            if (other.id == ignore_npc_id) continue;
            if (other.room != npc.room) continue;
            if (other.x == nx && other.y == ny) return false;
        }
        return true;
    };

    auto nearestPlayerForMonster = [&](const MonsterRuntime& mon, int& out_x, int& out_y) -> bool {
        int best_dist = 999999;
        bool found = false;
        for (const auto& [_, p] : players) {
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
    };

    auto tryMoveMonster = [&](MonsterRuntime& mon, const std::array<std::pair<int, int>, 4>& dirs) -> bool {
        for (const auto& [dx, dy] : dirs) {
            if (dx == 0 && dy == 0) continue;
            const int nx = mon.x + dx;
            const int ny = mon.y + dy;
            if (!canMonsterStand(mon, nx, ny, mon.id)) continue;
            mon.facing = facingFromDelta(dx, dy, mon.facing);
            mon.x = nx;
            mon.y = ny;
            return true;
        }
        return false;
    };

    auto rollChance = [](float p) -> bool {
        const float clamped = std::max(0.0f, std::min(1.0f, p));
        const float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        return r <= clamped;
    };

    auto findRespawnTile = [&](const std::string& room_name, int& out_x, int& out_y) -> bool {
        const Room* room = world_.getRoom(room_name);
        if (!room) return false;

        const int fallback_x = std::max(0, std::min(2, room->map_width() - 1));
        const int fallback_y = std::max(0, std::min(2, room->map_height() - 1));
        if (room->isWalkable(fallback_x, fallback_y) &&
            !occupiedByMonster(room_name, fallback_x, fallback_y) &&
            !occupiedByNpc(room_name, fallback_x, fallback_y)) {
            out_x = fallback_x;
            out_y = fallback_y;
            return true;
        }

        for (int y = 0; y < room->map_height(); ++y) {
            for (int x = 0; x < room->map_width(); ++x) {
                if (!room->isWalkable(x, y)) continue;
                if (occupiedByMonster(room_name, x, y)) continue;
                if (occupiedByNpc(room_name, x, y)) continue;
                out_x = x;
                out_y = y;
                return true;
            }
        }
        return false;
    };

    const auto tick_dt = std::chrono::milliseconds(kTickMs);
    auto last_tick = std::chrono::steady_clock::now();

    // Main server loop:
    // 1) drain ENet events (input/messages),
    // 2) advance fixed-rate combat/AI tick,
    // 3) broadcast updated game state when something changes.
    while (running_) {
        ENetEvent ev{};
        while (enet_host_service(server, &ev, 40) > 0) {
            if (ev.type == ENET_EVENT_TYPE_CONNECT) {
                std::cout << "[server] connect from ";
                printPeerIp(ev.peer->address);
                std::cout << "\n";
                players[ev.peer] = PlayerRuntime{ev.peer, false, PersistedPlayer{}, 100, 100, 60, 60, 2, 0, -1};
            }

            else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                try {
                    Envelope env = fromBytes<Envelope>(ev.packet->data, ev.packet->dataLength);

                    auto pit = players.find(ev.peer);
                    if (pit == players.end()) {
                        players[ev.peer] = PlayerRuntime{ev.peer, false, PersistedPlayer{}, 100, 100, 60, 60, 2, 0, -1};
                        pit = players.find(ev.peer);
                    }
                    PlayerRuntime& player = pit->second;

                    switch (env.type) {
                        case MsgType::Login: {
                            LoginMsg m = fromBytes<LoginMsg>(env.payload.data(), env.payload.size());

                            PersistedPlayer persisted;
                            std::string message;
                            const bool ok = authenticateLoginRequest(m, auth_db_, persisted, message);

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
                                pruneEquipmentForInventory(player);
                                player.hp = player.max_hp;
                                player.mana = player.max_mana;
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
                            if (dx != 0 || dy != 0) {
                                player.facing = facingFromDelta(dx, dy, player.facing);
                            }

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
                                occupiedByNpc(next_room, next_x, next_y) ||
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

                        case MsgType::Rotate: {
                            if (!player.authenticated) break;
                            RotateMsg m = fromBytes<RotateMsg>(env.payload.data(), env.payload.size());
                            const int dx = clampStep(m.dx);
                            const int dy = clampStep(m.dy);
                            if (dx == 0 && dy == 0) break;
                            player.facing = facingFromDelta(dx, dy, player.facing);
                            broadcastState(player.data.username + " turns");
                        } break;

                        case MsgType::Attack: {
                            if (!player.authenticated) break;
                            AttackMsg m = fromBytes<AttackMsg>(env.payload.data(), env.payload.size());
                            player.attack_target_monster_id = m.target_monster_id;
                            if (player.attack_target_monster_id >= 0) {
                                for (const auto& mon : monsters) {
                                    if (mon.id != player.attack_target_monster_id) continue;
                                    if (mon.room != player.data.room) break;
                                    player.facing = facingFromDelta(mon.x - player.data.x, mon.y - player.data.y, player.facing);
                                    break;
                                }
                            }

                            if (player.attack_target_monster_id >= 0) {
                                broadcastState(player.data.username + " targets monster " + std::to_string(player.attack_target_monster_id));
                            } else {
                                broadcastState(player.data.username + " stops attacking");
                            }
                        } break;

                        case MsgType::Pickup: {
                            if (!player.authenticated) break;
                            PickupMsg m = fromBytes<PickupMsg>(env.payload.data(), env.payload.size());

                            const int idx = findPickupCandidateIndex(player, m.item_id);

                            std::string event = player.data.username + " found no item";
                            if (idx >= 0) {
                                if (static_cast<int>(player.data.inventory.size()) >= kInventoryLimit) {
                                    event = player.data.username + " inventory is full";
                                    broadcastState(event);
                                    break;
                                }
                                if (parseCorpseMonsterId(items[idx].item_id).empty()) {
                                    player.data.inventory.push_back(items[idx].name);
                                } else {
                                    player.data.inventory.push_back(items[idx].item_id);
                                }
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
                                const Room* room = world_.getRoom(player.data.room);
                                if (!room) break;

                                int drop_x = player.data.x;
                                int drop_y = player.data.y;
                                if (m.to_x >= 0 && m.to_y >= 0) {
                                    drop_x = m.to_x;
                                    drop_y = m.to_y;
                                }
                                if (drop_x < 0 || drop_y < 0 ||
                                    drop_x >= room->map_width() || drop_y >= room->map_height()) {
                                    break;
                                }
                                if (!room->isWalkable(drop_x, drop_y)) break;
                                if (occupiedByMonster(player.data.room, drop_x, drop_y) ||
                                    occupiedByNpc(player.data.room, drop_x, drop_y) ||
                                    occupiedByPlayer(ev.peer, player.data.room, drop_x, drop_y)) {
                                    break;
                                }
                                const int throw_dist = tileDistance(player.data.x, player.data.y, drop_x, drop_y);
                                if (throw_dist > kThrowRangeTiles) break;

                                const std::string inv_entry = player.data.inventory[m.inventory_index];
                                GroundItemRuntime drop_item{};
                                drop_item.id = next_item_id++;
                                drop_item.room = player.data.room;
                                drop_item.x = drop_x;
                                drop_item.y = drop_y;

                                const std::string corpse_monster_id = parseCorpseMonsterId(inv_entry);
                                bool can_drop = false;
                                if (!corpse_monster_id.empty()) {
                                    auto dit = defs_by_id.find(corpse_monster_id);
                                    if (dit != defs_by_id.end()) {
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
                                    auto def_it = item_defs_by_id.find(item_id);
                                    if (def_it != item_defs_by_id.end()) {
                                        drop_item.item_id = def_it->second->id;
                                        drop_item.name = item_name;
                                        drop_item.sprite_tileset = def_it->second->sprite_tileset;
                                        drop_item.sprite_name = def_it->second->id;
                                    }
                                    can_drop = true;
                                }
                                if (!can_drop) break;

                                player.data.inventory.erase(player.data.inventory.begin() + m.inventory_index);
                                onInventoryErase(player, m.inventory_index);
                                items.push_back(std::move(drop_item));

                                event = player.data.username + " dropped " + items.back().name;
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
                                onInventorySwap(player, m.from_index, m.to_index);
                                savePlayerNow(player);
                                broadcastState(player.data.username + " rearranged inventory");
                            }
                        } break;

                        case MsgType::SetEquipment: {
                            if (!player.authenticated) break;
                            SetEquipmentMsg m = fromBytes<SetEquipmentMsg>(env.payload.data(), env.payload.size());
                            const std::string equip_type = canonicalEquipType(m.equip_type);
                            if (equip_type.empty()) break;

                            if (m.inventory_index < 0) {
                                player.data.equipment_by_type.erase(equip_type);
                                savePlayerNow(player);
                                broadcastState(player.data.username + " unequipped " + equip_type);
                                break;
                            }

                            if (m.inventory_index >= static_cast<int>(player.data.inventory.size())) break;
                            const std::string& inv_item_name = player.data.inventory[m.inventory_index];
                            auto it = item_defs_by_id.find(normalizeId(inv_item_name));
                            if (it == item_defs_by_id.end()) break;
                            const std::string item_type = canonicalEquipType(it->second->item_type);
                            if (item_type.empty() || item_type != equip_type) break;

                            player.data.equipment_by_type[equip_type] = m.inventory_index;
                            savePlayerNow(player);
                            broadcastState(player.data.username + " equipped " + inv_item_name);
                        } break;

                        case MsgType::MoveGroundItem: {
                            if (!player.authenticated) break;
                            MoveGroundItemMsg m = fromBytes<MoveGroundItemMsg>(env.payload.data(), env.payload.size());
                            const Room* room = world_.getRoom(player.data.room);
                            if (!room) break;

                            if (m.to_x < 0 || m.to_y < 0 || m.to_x >= room->map_width() || m.to_y >= room->map_height()) {
                                break;
                            }
                            if (!room->isWalkable(m.to_x, m.to_y)) break;
                            if (occupiedByMonster(player.data.room, m.to_x, m.to_y) ||
                                occupiedByNpc(player.data.room, m.to_x, m.to_y) ||
                                occupiedByPlayer(ev.peer, player.data.room, m.to_x, m.to_y)) {
                                break;
                            }

                            const int idx = findGroundItemIndexById(player, m.item_id);
                            if (idx < 0) break;
                            if (!isItemReachableByPlayer(player, items[(size_t)idx])) break;

                            const int throw_dist = tileDistance(m.to_x, m.to_y, player.data.x, player.data.y);
                            if (throw_dist > kThrowRangeTiles) break;

                            items[idx].x = m.to_x;
                            items[idx].y = m.to_y;
                            broadcastState(player.data.username + " moved " + items[idx].name);
                        } break;

                        case MsgType::Chat: {
                            if (!player.authenticated) break;
                            ChatMsg m = fromBytes<ChatMsg>(env.payload.data(), env.payload.size());
                            std::string text = m.text;
                            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
                            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
                            if (text.empty()) break;
                            if (text.size() > 120) text.resize(120);
                            std::string speech_type = normalizeId(m.speech_type);
                            if (speech_type != "talk" && speech_type != "think" && speech_type != "yell") {
                                speech_type = "talk";
                            }

                            ChatMsg out{player.data.username, speech_type, text};
                            for (const auto& [peer, p] : players) {
                                if (!p.authenticated) continue;
                                if (p.data.room != player.data.room) continue;
                                sendWire(peer, 0, pack(MsgType::Chat, out));
                            }

                            auto normalizeText = [&](std::string s) -> std::string {
                                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                                    return static_cast<char>(std::tolower(c));
                                });
                                for (char& c : s) {
                                    if (!(std::isalnum(static_cast<unsigned char>(c)) || std::isspace(static_cast<unsigned char>(c)))) {
                                        c = ' ';
                                    }
                                }
                                while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
                                while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
                                return s;
                            };

                            const std::string norm_text = normalizeText(text);
                            auto containsTopic = [&](const std::string& hay, const std::string& needle) -> bool {
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
                            for (size_t i = 0; i < npcs.size(); ++i) {
                                const auto& npc = npcs[i];
                                if (npc.room != player.data.room) continue;
                                const int dist = tileDistance(player.data.x, player.data.y, npc.x, npc.y);
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
                                speaker_dx = player.data.x - npc.x;
                                speaker_dy = player.data.y - npc.y;
                            }
                            if (best_idx >= 0 && !npc_reply.empty() && !npc_name.empty()) {
                                npcs[(size_t)best_idx].facing = facingFromDelta(
                                    speaker_dx,
                                    speaker_dy,
                                    npcs[(size_t)best_idx].facing
                                );
                                npcs[(size_t)best_idx].talk_pause_ms = std::max(npcs[(size_t)best_idx].talk_pause_ms, 3000);
                                ChatMsg npc_chat{npc_name, "talk", npc_reply};
                                for (const auto& [peer, p] : players) {
                                    if (!p.authenticated) continue;
                                    if (p.data.room != player.data.room) continue;
                                    sendWire(peer, 0, pack(MsgType::Chat, npc_chat));
                                }
                            }
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
                if (dist > kMeleeRangeTiles) {
                    continue;
                }

                p.facing = facingFromDelta(mon.x - p.data.x, mon.y - p.data.y, p.facing);
                p.attack_anim_seq += 1;
                mon.facing = facingFromDelta(p.data.x - mon.x, p.data.y - mon.y, mon.facing);
                mon.attack_anim_seq += 1;

                const int dmg = kCombatDamagePerTick;
                mon.hp = std::max(0, mon.hp - dmg);
                changed = true;
                tick_event = p.data.username + " hits " + mon.name + " for " + std::to_string(dmg);

                if (mon.hp <= 0) {
                    p.data.exp += mon.exp_reward;
                    p.attack_target_monster_id = -1;
                    items.push_back(GroundItemRuntime{
                        next_item_id++,
                        makeCorpseItemId(mon.def_id.empty() ? mon.name : mon.def_id),
                        mon.name + " corpse",
                        mon.sprite_tileset,
                        mon.sprite_name,
                        std::max(1, mon.size_w),
                        std::max(1, mon.size_h),
                        1,
                        mon.room,
                        mon.x,
                        mon.y
                    });
                    for (const auto& drop : mon.drops) {
                        if (drop.item_id.empty()) continue;
                        if (!rollChance(drop.chance)) continue;
                        spawnItemById(drop.item_id, mon.room, mon.x, mon.y);
                    }
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

                std::array<std::pair<int, int>, 4> dirs = {{
                    {0, -1}, {1, 0}, {0, 1}, {-1, 0}
                }};
                const int start = std::rand() % 4;
                std::array<std::pair<int, int>, 4> random_dirs = {{
                    dirs[(start + 0) % 4],
                    dirs[(start + 1) % 4],
                    dirs[(start + 2) % 4],
                    dirs[(start + 3) % 4]
                }};

                bool moved = false;
                int px = 0;
                int py = 0;
                if (nearestPlayerForMonster(mon, px, py)) {
                    std::array<std::pair<int, int>, 4> chase_dirs = {};
                    int idx = 0;
                    auto push_unique = [&](int dx, int dy) {
                        if (dx == 0 && dy == 0) return;
                        for (int i = 0; i < idx; ++i) {
                            if (chase_dirs[(size_t)i].first == dx && chase_dirs[(size_t)i].second == dy) return;
                        }
                        if (idx < 4) chase_dirs[(size_t)idx++] = {dx, dy};
                    };

                    const int dx_to_player = px - mon.x;
                    const int dy_to_player = py - mon.y;
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
                    moved = tryMoveMonster(mon, chase_dirs);
                } else {
                    // 25% chance to idle this tick to avoid jittery movement.
                    if ((std::rand() % kMonsterIdleChanceDivisor) != 0) {
                        moved = tryMoveMonster(mon, random_dirs);
                    }
                }

                if (moved) {
                    changed = true;
                    if (tick_event.empty()) tick_event = "monsters moved";
                }
            }

            for (auto& npc : npcs) {
                if (npc.talk_pause_ms > 0) {
                    npc.talk_pause_ms = std::max(0, npc.talk_pause_ms - static_cast<int>(tick_dt.count()));
                    continue;
                }
                npc.move_accum_ms += static_cast<int>(tick_dt.count());
                if (npc.move_accum_ms < std::max(1, npc.speed_ms)) continue;
                npc.move_accum_ms = 0;

                std::array<std::pair<int, int>, 4> dirs = {{
                    {0, -1}, {1, 0}, {0, 1}, {-1, 0}
                }};
                const int start = std::rand() % 4;
                std::array<std::pair<int, int>, 4> random_dirs = {{
                    dirs[(start + 0) % 4],
                    dirs[(start + 1) % 4],
                    dirs[(start + 2) % 4],
                    dirs[(start + 3) % 4]
                }};
                bool moved = false;
                for (const auto& [dx, dy] : random_dirs) {
                    const int nx = npc.x + dx;
                    const int ny = npc.y + dy;
                    if (!canNpcStand(npc, nx, ny, npc.id)) continue;
                    npc.facing = facingFromDelta(dx, dy, npc.facing);
                    npc.x = nx;
                    npc.y = ny;
                    moved = true;
                    break;
                }
                if (moved) {
                    changed = true;
                    if (tick_event.empty()) tick_event = "npcs moved";
                }
            }

            const MonsterCombatResult monster_combat = resolveMonsterAttacks(
                players,
                monsters,
                findRespawnTile,
                kMeleeRangeTiles
            );
            if (monster_combat.changed) changed = true;
            if (!monster_combat.event_text.empty()) tick_event = monster_combat.event_text;
            for (ENetPeer* defeated_peer : monster_combat.defeated_players) {
                auto pit = players.find(defeated_peer);
                if (pit == players.end()) continue;
                savePlayerNow(pit->second);
            }

            if (changed) {
                broadcastState(tick_event.empty() ? "combat tick" : tick_event);
            }
        }
    }

    for (const auto& [_, p] : players) savePlayerNow(p);
    enet_host_destroy(server);
}
