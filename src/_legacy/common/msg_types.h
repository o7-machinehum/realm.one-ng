// Network message type discriminator shared by client and server.
#pragma once

#include <cstdint>

enum class MsgType : uint16_t {
    Login = 1,
    Chat,
    ChatSend,
    Room,
    LoginResult,
    Move,
    Rotate,
    Attack,
    Pickup,
    Drop,
    InventorySwap,
    SetEquipment,
    MoveGroundItem,
    GameState
};
