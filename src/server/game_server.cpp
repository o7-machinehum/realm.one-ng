#include "game_server.h"

#include "auth_crypto.h"
#include "auth_db.h"

#include <enet/enet.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <vector>

namespace gs {

namespace {

constexpr int kHostChannels = 1;

struct PlayerSession {
    net::PlayerSnapshot snapshot;
    bool authenticated = false;
    bool dirty         = false;
    std::string username;     // populated on successful auth; used to save state
};

bool isStandableAt(const voxel::World& world, int x, int y, int z) {
    const auto top = world.topCubeZ(x, y);
    return top.has_value() && static_cast<int>(*top) + 1 == z;
}

uint8_t facingFromMove(int8_t dx, int8_t dy) {
    if (dy < 0) return static_cast<uint8_t>(net::Facing::North);
    if (dy > 0) return static_cast<uint8_t>(net::Facing::South);
    if (dx < 0) return static_cast<uint8_t>(net::Facing::West);
    return static_cast<uint8_t>(net::Facing::East);
}

bool isCardinalStep(int8_t dx, int8_t dy) {
    if (dx == 0 && dy == 0) return false;
    if (dx != 0 && dy != 0) return false;
    return std::abs(dx) <= 1 && std::abs(dy) <= 1;
}

std::optional<uint32_t> standingZ(const voxel::World& world, int x, int y) {
    const auto top = world.topCubeZ(x, y);
    if (!top.has_value()) return std::nullopt;
    const uint32_t z = *top + 1u;
    if (z >= world.header.size_z) return std::nullopt;
    return z;
}

bool isWithinStepHeight(uint32_t current_z, uint32_t target_z, int max_step = 1) {
    const int diff = static_cast<int>(target_z) - static_cast<int>(current_z);
    return std::abs(diff) <= max_step;
}

uint32_t playerIdForPeer(const ENetPeer* peer) {
    return static_cast<uint32_t>(peer->incomingPeerID) + 1u;
}

ENetPacket* makePacket(const std::vector<uint8_t>& bytes, bool reliable) {
    return enet_packet_create(bytes.data(), bytes.size(),
                              reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
}

void sendTo(ENetPeer* peer, const std::vector<uint8_t>& bytes, bool reliable) {
    enet_peer_send(peer, 0, makePacket(bytes, reliable));
}

void broadcast(ENetHost* host, const std::vector<uint8_t>& bytes, bool reliable) {
    enet_host_broadcast(host, 0, makePacket(bytes, reliable));
}

} // namespace

struct GameServer::Impl {
    ServerOptions opts;
    voxel::World world;
    std::unique_ptr<authdb::AuthDb> auth_db;
    ENetHost* host = nullptr;
    std::unordered_map<uint32_t, PlayerSession> players;
    std::unordered_map<ENetPeer*, uint32_t>     peer_to_id;

    explicit Impl(ServerOptions o) : opts(std::move(o)) {}
    ~Impl() {
        if (host) enet_host_destroy(host);
        enet_deinitialize();
    }

    bool loadWorld() {
        std::string err;
        if (!voxel::load(world, opts.world_path, &err)) {
            std::fprintf(stderr, "FATAL: load %s: %s\n", opts.world_path.c_str(), err.c_str());
            return false;
        }
        std::printf("loaded %s (%ux%ux%u)\n",
                    opts.world_path.c_str(),
                    world.header.size_x, world.header.size_y, world.header.size_z);
        return true;
    }

    bool openAuthDb() {
        try {
            auth_db = std::make_unique<authdb::AuthDb>(opts.db_path);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "FATAL: open auth db: %s\n", e.what());
            return false;
        }
        std::printf("auth db: %s\n", opts.db_path.c_str());
        return true;
    }

    bool initEnet() {
        if (enet_initialize() != 0) {
            std::fprintf(stderr, "FATAL: enet_initialize\n");
            return false;
        }
        ENetAddress addr{};
        addr.host = ENET_HOST_ANY;
        addr.port = opts.port;
        host = enet_host_create(&addr, opts.max_clients, kHostChannels, 0, 0);
        if (!host) {
            std::fprintf(stderr, "FATAL: enet_host_create on port %u\n", opts.port);
            return false;
        }
        std::printf("listening on port %u\n", opts.port);
        return true;
    }

    std::optional<net::PlayerSnapshot> findSpawnSnapshot(uint32_t player_id, const std::string& name) {
        const int cx = static_cast<int>(world.header.size_x) / 2;
        const int cy = static_cast<int>(world.header.size_y) / 2;
        for (int r = 0; r <= opts.spawn_search_radius; ++r) {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    const auto z = standingZ(world, cx + dx, cy + dy);
                    if (!z) continue;
                    return net::PlayerSnapshot{
                        player_id,
                        cx + dx, cy + dy, static_cast<int32_t>(*z),
                        static_cast<uint8_t>(net::Facing::South),
                        name,
                    };
                }
            }
        }
        return std::nullopt;
    }

    void sendWorldStateTo(ENetPeer* peer, uint32_t my_id) {
        sendTo(peer, net::writeWelcome(my_id,
                                       world.header.size_x,
                                       world.header.size_y,
                                       world.header.size_z), true);
        for (auto& kv : players) {
            sendTo(peer, net::writePlayerState(kv.second.snapshot), true);
        }
    }

    void onConnect(ENetPeer* peer) {
        const uint32_t pid = playerIdForPeer(peer);
        players[pid] = PlayerSession{};
        players[pid].snapshot.id = pid;
        peer_to_id[peer] = pid;
        std::printf("connect pid=%u (pending auth)\n", pid);
    }

    void persistSession(const PlayerSession& session) {
        if (!session.authenticated || session.username.empty()) return;
        const authdb::PersistedState s{
            session.snapshot.x, session.snapshot.y, session.snapshot.z,
            session.snapshot.facing,
        };
        if (!auth_db->saveState(session.username, s)) {
            std::fprintf(stderr, "warn: failed to save state for %s\n", session.username.c_str());
        }
    }

    void onDisconnect(ENetPeer* peer) {
        const auto it = peer_to_id.find(peer);
        if (it == peer_to_id.end()) return;
        const uint32_t pid = it->second;
        const PlayerSession& session = players[pid];
        const bool was_authed = session.authenticated;
        persistSession(session);
        std::printf("disconnect pid=%u\n", pid);
        players.erase(pid);
        peer_to_id.erase(it);
        if (was_authed) broadcast(host, net::writePlayerLeave(pid), true);
    }

    bool verifyLoginSignature(const net::LoginPayload& l) const {
        const auto payload = authx::makeAuthPayload(l.username, l.public_key_hex, l.create_account);
        return authx::verifyEd25519(l.public_key_hex, payload, l.signature_hex);
    }

    void rejectLogin(ENetPeer* peer, const std::string& reason) {
        sendTo(peer, net::writeLoginResult(false, reason), true);
    }

    std::optional<net::PlayerSnapshot> snapshotForReturningPlayer(
            uint32_t player_id, const std::string& username) const {
        const auto saved = auth_db->loadState(username);
        if (!saved) return std::nullopt;
        if (!isStandableAt(world, saved->x, saved->y, saved->z)) return std::nullopt;
        return net::PlayerSnapshot{
            player_id, saved->x, saved->y, saved->z, saved->facing, username,
        };
    }

    void acceptLogin(ENetPeer* peer, PlayerSession& session,
                     const std::string& username,
                     const std::string& welcome_msg) {
        const auto resumed = snapshotForReturningPlayer(session.snapshot.id, username);
        const auto fresh   = resumed ? std::nullopt : findSpawnSnapshot(session.snapshot.id, username);
        const auto snap    = resumed ? resumed : fresh;
        if (!snap) { rejectLogin(peer, "no spawn available"); return; }

        session.snapshot      = *snap;
        session.username      = username;
        session.authenticated = true;
        session.dirty         = false;

        sendTo(peer, net::writeLoginResult(true, welcome_msg), true);
        sendWorldStateTo(peer, session.snapshot.id);
        broadcast(host, net::writePlayerState(session.snapshot), true);

        std::printf("login pid=%u user=%s %s=(%d,%d,%d)\n",
                    session.snapshot.id, username.c_str(),
                    resumed ? "resumed" : "spawn",
                    snap->x, snap->y, snap->z);
    }

    void handleLogin(ENetPeer* peer, PlayerSession& session, net::Reader& r) {
        if (session.authenticated) return;          // ignore re-login
        net::LoginPayload payload;
        if (!net::readLogin(r, payload)) { rejectLogin(peer, "malformed login"); return; }
        if (!verifyLoginSignature(payload))   { rejectLogin(peer, "bad signature"); return; }

        const auto outcome = auth_db->tryLogin(payload.username,
                                               payload.public_key_hex,
                                               payload.create_account);
        if (!outcome.success) { rejectLogin(peer, outcome.message); return; }
        acceptLogin(peer, session, payload.username, outcome.message);
    }

    void handleMove(PlayerSession& session, net::Reader& r) {
        if (!session.authenticated) return;
        int8_t dx = 0, dy = 0;
        if (!net::readMove(r, dx, dy)) return;
        if (!isCardinalStep(dx, dy)) return;

        const int nx = session.snapshot.x + dx;
        const int ny = session.snapshot.y + dy;
        const auto target_z = standingZ(world, nx, ny);
        if (!target_z) return;
        if (!isWithinStepHeight(static_cast<uint32_t>(session.snapshot.z), *target_z)) return;

        session.snapshot.x = nx;
        session.snapshot.y = ny;
        session.snapshot.z = static_cast<int32_t>(*target_z);
        session.snapshot.facing = facingFromMove(dx, dy);
        session.dirty = true;
    }

    void onReceive(ENetPeer* peer, ENetPacket* pkt) {
        const auto it = peer_to_id.find(peer);
        if (it == peer_to_id.end()) return;
        PlayerSession& session = players[it->second];

        net::Reader r(pkt->data, pkt->dataLength);
        const auto type = static_cast<net::MsgType>(r.u8());
        switch (type) {
            case net::MsgType::Login: handleLogin(peer, session, r); break;
            case net::MsgType::Move:  handleMove(session, r);        break;
            default: break;
        }
    }

    void broadcastDirty() {
        for (auto& kv : players) {
            if (!kv.second.dirty) continue;
            broadcast(host, net::writePlayerState(kv.second.snapshot), true);
            kv.second.dirty = false;
        }
    }

    void serviceOnce() {
        ENetEvent ev;
        while (enet_host_service(host, &ev, opts.pump_timeout_ms) > 0) {
            switch (ev.type) {
                case ENET_EVENT_TYPE_CONNECT:    onConnect(ev.peer); break;
                case ENET_EVENT_TYPE_RECEIVE:
                    onReceive(ev.peer, ev.packet);
                    enet_packet_destroy(ev.packet);
                    break;
                case ENET_EVENT_TYPE_DISCONNECT: onDisconnect(ev.peer); break;
                default: break;
            }
        }
    }
};

GameServer::GameServer(ServerOptions opts) : impl_(std::make_unique<Impl>(std::move(opts))) {}
GameServer::~GameServer() = default;

bool GameServer::start() {
    return impl_->loadWorld() && impl_->openAuthDb() && impl_->initEnet();
}

void GameServer::runForever() {
    while (true) {
        impl_->serviceOnce();
        impl_->broadcastDirty();
    }
}

} // namespace gs
