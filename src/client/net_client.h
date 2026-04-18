#pragma once

#include "net_msgs.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace netc {

struct WelcomeEvent {
    uint32_t your_id;
    uint32_t world_size_x;
    uint32_t world_size_y;
    uint32_t world_size_z;
};

using IncomingMsg = std::variant<
    WelcomeEvent,
    net::LoginResultEvent,
    net::PlayerSnapshot,
    net::PlayerLeaveEvent
>;

class NetClient {
public:
    NetClient();
    ~NetClient();
    NetClient(const NetClient&) = delete;
    NetClient& operator=(const NetClient&) = delete;

    bool connect(const std::string& host, uint16_t port, int timeout_ms = 3000);
    void disconnect();
    bool connected() const;

    void pump(int timeout_ms = 0);
    std::optional<IncomingMsg> pop();

    void sendLogin(const net::LoginPayload& payload);
    void sendMove(int8_t dx, int8_t dy);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace netc
