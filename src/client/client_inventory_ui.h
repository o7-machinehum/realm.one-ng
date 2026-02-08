#pragma once

#include "client_support.h"
#include "msg.h"
#include "raylib.h"

#include <optional>

namespace client {

struct InventoryUiConfig {
    int bottom_panel_height = 180;
    float panel_w = 260.0f;
    int cols = 4;
    float slot_size = 48.0f;
    float gap = 10.0f;
};

struct InventoryUiState {
    DragState drag;
};

struct InventoryUiOutput {
    std::optional<InventorySwapMsg> swap_msg;
    std::optional<DropMsg> drop_msg;
};

void drawInventoryUi(Font ui_font,
                     const GameStateMsg& game_state,
                     InventoryUiState& state,
                     const InventoryUiConfig& cfg,
                     InventoryUiOutput& out);

} // namespace client
