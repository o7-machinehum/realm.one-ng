#pragma once

#include "client_support.h"
#include "msg.h"
#include "raylib.h"

#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <string>

namespace client {

constexpr int kHotbarSlots = 10;
constexpr int kEquipSlotCount = 8;
constexpr float kHudSectionGap = 16.0f; // gap between equipment and hotbar
constexpr float kHudIdleAlpha = 0.15f;
constexpr float kHudActiveAlpha = 0.9f;
// Compute integer pixel-art scale from screen height (crisp at any resolution)
inline float widgetScale(float screen_h) {
    return std::max(1.0f, std::floor(screen_h / 360.0f));
}

// Fixed equipment slot order
inline const char* kEquipSlotTypes[kEquipSlotCount] = {
    "Armor", "Weapon", "Ring", "Legs", "Boots", "Helmet", "Necklace", "Shield"
};

struct HudState {
    std::array<int, kHotbarSlots> hotbar_slots{}; // inventory indices, -1 = empty
    DragState drag;
    float hover_alpha = 0.0f; // current opacity (0..1), lerps toward target

    HudState() { hotbar_slots.fill(-1); }
};

struct HudOutput {
    std::optional<InventorySwapMsg> swap_msg;
    std::optional<DropMsg> drop_msg;
    std::optional<SetEquipmentMsg> set_equipment_msg;
    std::optional<PickupMsg> pickup_ground_item;
    int hotbar_activate = -1; // hotbar slot activated by keypress
};

// Returns the total HUD height in pixels (tallest widget at scale).
float hudTotalHeight(float screen_h);

// Draws the bottom HUD (equipment bar + hotbar, both sprite-based, HP/MP top-left).
void drawHud(Font ui_font,
             const GameStateMsg& game_state,
             HudState& state,
             Texture2D hotbar_tex,
             Texture2D equip_tex,
             int dragging_ground_item_id,
             bool overlay_visible,
             float bottom_margin,
             float dt,
             const std::function<bool(const std::string&, const Rectangle&, Color)>& draw_item_icon,
             const std::function<std::string(const std::string&)>& resolve_item_equip_type,
             HudOutput& out);

} // namespace client
