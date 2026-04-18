#pragma once

#include "enet_compat.h"
#include <thread>
#include <atomic>

#include "msg.h"

class NetClient {
public:
    NetClient(Mailbox& mailbox, std::string host, uint16_t port)
        : mailbox_(mailbox), hostname_(host), port_(port)  {}

    void start() {
        recv_thread_ = std::thread(&NetClient::recvLoop, this);
    }

    void stop() {
        running_ = false;
        if (recv_thread_.joinable())
            recv_thread_.join();
    }

private:
    Mailbox& mailbox_;
    std::thread recv_thread_;
    std::atomic<bool> running_{true};

    void recvLoop();
    std::string hostname_;
    uint16_t port_;
};
