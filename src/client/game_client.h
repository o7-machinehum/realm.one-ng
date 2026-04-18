#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace gc {

struct ClientOptions {
    std::string world_path = "data/world.dat";
    std::string host       = "127.0.0.1";
    uint16_t    port       = 7000;
    std::string player_name = "player";
    int         window_w   = 1280;
    int         window_h   = 800;
};

class GameClient {
public:
    explicit GameClient(ClientOptions opts);
    ~GameClient();
    GameClient(const GameClient&) = delete;
    GameClient& operator=(const GameClient&) = delete;

    bool init();
    void runUntilClosed();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gc
