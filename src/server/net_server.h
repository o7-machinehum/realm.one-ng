// Top-level network server: owns the ENet host and the game-loop thread.
#pragma once

#include "enet_compat.h"
#include <thread>
#include <stop_token>

#include "msg.h"
#include "envelope.h"

class World;
class AuthDb;

// Top-level network server: owns the ENet host and runs the game loop on a background thread.
class NetServer {
public:
    NetServer(World& world, AuthDb& auth_db, uint16_t port)
        : world_(world), auth_db_(auth_db), port_(port) {}

    // Starts the game loop on a background thread.
    void start() {
        thread_ = std::jthread([this](std::stop_token st) { recvLoop(st); });
    }

    // Requests the game loop to stop and waits for the thread to finish.
    void stop() {
        thread_.request_stop();
        if (thread_.joinable()) thread_.join();
    }

private:
    World& world_;
    AuthDb& auth_db_;
    uint16_t port_;
    std::jthread thread_;

    // Main receive/tick loop running on the background thread.
    void recvLoop(std::stop_token stop);
};
