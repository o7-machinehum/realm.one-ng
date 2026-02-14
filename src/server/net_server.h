// Top-level network server: owns the ENet host and the game-loop thread.
#pragma once

#include <enet/enet.h>
#include <thread>
#include <stop_token>

#include "msg.h"
#include "envelope.h"

class World;
class AuthDb;

class NetServer {
public:
    NetServer(World& world, AuthDb& auth_db, uint16_t port)
        : world_(world), auth_db_(auth_db), port_(port) {}

    void start() {
        thread_ = std::jthread([this](std::stop_token st) { recvLoop(st); });
    }

    void stop() {
        thread_.request_stop();
        if (thread_.joinable()) thread_.join();
    }

private:
    World& world_;
    AuthDb& auth_db_;
    uint16_t port_;
    std::jthread thread_;

    void recvLoop(std::stop_token stop);
};
