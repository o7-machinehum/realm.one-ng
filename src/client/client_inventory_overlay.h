#pragma once

#include "client_support.h"
#include "container_def.h"
#include "item_defs.h"
#include "msg.h"
#include "raylib.h"

#include <functional>
#include <optional>
#include <string>

namespace client {

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
                          Texture2D backpack_tex,
                          const ContainerDef& backpack_def,
                          int backpack_index_offset,
                          const std::function<bool(const std::string&, const Rectangle&, Color)>& draw_item_icon,
                          const std::function<std::optional<ItemType>(const std::string&)>& resolve_item_equip_type,
                          InventoryOverlayOutput& out);

} // namespace client
