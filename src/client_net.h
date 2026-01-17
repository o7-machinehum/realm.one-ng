#pragma once
#include <enet/enet.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct RemotePlayer {
    std::string name;
    int x = 0;
    int y = 0;
};

class ClientNet {
public:
    ClientNet();
    ~ClientNet();

    bool connectTo(const std::string& ip, int port);
    void disconnect();

    void tick(); // pump ENet

    void sendLine(const std::string& line);

    bool connected() const { return peer_ != nullptr; }
    bool authed() const { return authed_; }
    const std::string& myName() const { return myName_; }

    // These are updated by server messages
    std::optional<std::string> takeRoomTmx(); // once when received
    std::string currentRoom() const { return room_; }
    int myX() const { return myX_; }
    int myY() const { return myY_; }
    const std::vector<RemotePlayer>& others() const { return others_; }

    // For UI
    std::optional<std::string> takeStatusLine();

private:
    void handleLine(const std::string& line);

    ENetHost* host_ = nullptr;
    ENetPeer* peer_ = nullptr;

    bool authed_ = false;
    std::string myName_;
    std::string room_;
    int myX_ = 0, myY_ = 0;
    std::vector<RemotePlayer> others_;

    // Room transfer state
    bool expectingRoomBlob_ = false;
    size_t roomBytesExpected_ = 0;
    std::string roomNamePending_;
    std::string roomBlob_;

    std::optional<std::string> roomTmxReady_;
    std::optional<std::string> status_;
};
