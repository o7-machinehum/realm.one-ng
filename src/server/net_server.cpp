#include "net_server.h"

#include "server_msg_handlers.h"
#include "server_spawning.h"
#include "server_state.h"
#include "server_tick.h"
#include "server_util.h"
#include "string_util.h"
#include "world.h"

#include <chrono>
#include <iostream>

void NetServer::recvLoop(std::stop_token stop) {
    ENetAddress addr{};
    addr.host = ENET_HOST_ANY;
    addr.port = port_;

    ENetHost* server = enet_host_create(&addr, 64, 2, 0, 0);
    if (!server) {
        std::cerr << "[server] failed to create ENet host\n";
        return;
    }
    std::cout << "[server] listening on port " << port_ << "\n";

    // ---- Initialise shared game state ----
    ServerState state;
    state.world   = &world_;
    state.auth_db = &auth_db_;
    state.host    = server;

    state.monster_defs_storage = loadMonsterDefs("game/monsters");
    for (const auto& d : state.monster_defs_storage)
        state.monster_defs_by_id[normalizeId(d.id)] = &d;
    if (state.monster_defs_by_id.empty())
        std::cerr << "[server] warning: no monster defs found in game/monsters\n";

    state.npc_defs_storage = loadNpcDefs("game/npcs");
    for (const auto& d : state.npc_defs_storage)
        state.npc_defs_by_id[normalizeId(d.id)] = &d;
    if (state.npc_defs_by_id.empty())
        std::cerr << "[server] warning: no npc defs found in game/npcs\n";

    state.item_defs_storage = loadItemDefs("game/items");
    for (const auto& d : state.item_defs_storage) {
        state.item_defs_by_id[normalizeId(d.id)]   = &d;
        state.item_defs_by_id[normalizeId(d.name)] = &d;
    }
    if (state.item_defs_by_id.empty())
        std::cerr << "[server] warning: no item defs found in game/items\n";

    state.settings = loadGlobalSettings("data/global.toml");
    spawnInitialEntities(state);

    // ---- Main server loop ----
    constexpr int kTickMs = 500;
    const auto tick_dt = std::chrono::milliseconds(kTickMs);
    auto last_tick = std::chrono::steady_clock::now();

    while (!stop.stop_requested()) {
        ENetEvent ev{};
        while (enet_host_service(server, &ev, 40) > 0) {
            if (ev.type == ENET_EVENT_TYPE_CONNECT) {
                std::cout << "[server] connect from ";
                logPeerAddress(ev.peer->address);
                std::cout << "\n";
                state.players[ev.peer] = PlayerRuntime{};
                state.players[ev.peer].peer = ev.peer;
            }

            else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                try {
                    Envelope env = fromBytes<Envelope>(ev.packet->data, ev.packet->dataLength);
                    auto pit = state.players.find(ev.peer);
                    if (pit == state.players.end()) {
                        state.players[ev.peer] = PlayerRuntime{};
                        state.players[ev.peer].peer = ev.peer;
                    }
                    dispatchMessage(state, ev.peer, env);
                } catch (const std::exception& e) {
                    std::cerr << "[server] decode error: " << e.what() << "\n";
                }
                enet_packet_destroy(ev.packet);
            }

            else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
                auto it = state.players.find(ev.peer);
                if (it != state.players.end()) {
                    if (it->second.authenticated) {
                        persistPlayer(it->second, *state.auth_db);
                        std::cout << "[server] disconnect user=" << it->second.data.username << "\n";
                    } else {
                        std::cout << "[server] disconnect\n";
                    }
                    state.players.erase(it);
                    broadcastGameState(state, "A player disconnected");
                }
            }
        }

        // ---- Fixed-rate game tick ----
        const auto now = std::chrono::steady_clock::now();
        while (now - last_tick >= tick_dt) {
            last_tick += tick_dt;
            auto tick_result = advanceServerTick(state, kTickMs);
            if (tick_result.state_changed) {
                broadcastGameState(state, tick_result.event_text.empty() ? "combat tick" : tick_result.event_text);
            }
        }
    }

    // ---- Cleanup ----
    for (const auto& [_, p] : state.players) persistPlayer(p, *state.auth_db);
    enet_host_destroy(server);
}
