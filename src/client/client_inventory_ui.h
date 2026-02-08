#pragma once

#include "client_support.h"
#include "msg.h"
#include "raylib.h"

#include <optional>
#include <functional>

namespace client {

struct InventoryUiConfig {
    int bottom_panel_height = 180;
    int top_offset = 72;
    float panel_w = 300.0f;
    int cols = 4;
    float slot_size = 42.0f;
    float gap = 8.0f;
};

struct InventoryUiState {
    DragState drag;
};

struct InventoryUiOutput {
    std::optional<InventorySwapMsg> swap_msg;
    std::optional<DropMsg> drop_msg;
    std::optional<SetEquipmentMsg> set_equipment_msg;
};

void drawInventoryUi(Font ui_font,
                     const GameStateMsg& game_state,
                     InventoryUiState& state,
                     const InventoryUiConfig& cfg,
                     const Rectangle& panel_rect,
                     const std::function<bool(const std::string&, const Rectangle&)>& draw_item_icon,
                     const std::function<std::string(const std::string&)>& resolve_item_equip_type,
                     InventoryUiOutput& out);

} // namespace client
