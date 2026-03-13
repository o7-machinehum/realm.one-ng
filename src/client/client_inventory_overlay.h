#pragma once

#include "client_support.h"
#include "msg.h"
#include "raylib.h"

#include <functional>
#include <optional>
#include <string>

namespace client {

constexpr int kBackpackSlots = 20;
constexpr int kBackpackCols = 5;
constexpr int kBackpackRows = 4;
constexpr int kBackpackIndexOffset = 8; // backpack slot j → inventory[8+j]
constexpr float kOverlaySlotSize = 48.0f;
constexpr float kOverlaySlotGap = 6.0f;
constexpr float kOverlayFadeDuration = 0.15f;

struct InventoryOverlayState {
    bool visible = false;
    float alpha = 0.0f;
};

struct InventoryOverlayOutput {
    std::optional<InventorySwapMsg> swap_msg;
    std::optional<DropMsg> drop_msg;
    std::optional<SetEquipmentMsg> set_equipment_msg;
};

// Ticks the overlay fade animation. Call every frame.
void tickInventoryOverlay(InventoryOverlayState& state, float dt);

// Draws the inventory overlay (backpack grid + skills).
// drag: shared DragState (also used by HUD for cross-area drag).
void drawInventoryOverlay(Font ui_font,
                          const GameStateMsg& game_state,
                          InventoryOverlayState& state,
                          DragState& drag,
                          const std::function<bool(const std::string&, const Rectangle&, Color)>& draw_item_icon,
                          const std::function<std::string(const std::string&)>& resolve_item_equip_type,
                          InventoryOverlayOutput& out);

} // namespace client
