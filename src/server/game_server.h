#pragma once

#include "net_msgs.h"
#include "voxel_world.h"

#include <cstdint>
#include <memory>
#include <string>

namespace gs {

struct ServerOptions {
    std::string world_path = "data/world.dat";
    std::string db_path    = "data/auth.db";
    uint16_t    port       = 7000;
    int         max_clients = 32;
    int         pump_timeout_ms = 16;
    int         spawn_search_radius = 8;
};

class GameServer {
public:
    explicit GameServer(ServerOptions opts);
    ~GameServer();
    GameServer(const GameServer&) = delete;
    GameServer& operator=(const GameServer&) = delete;

    bool start();
    void runForever();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gs
