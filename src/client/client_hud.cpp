#include "client_hud.h"
#include "client_game_ui.h"
#include "item_defs.h"

#include <algorithm>
#include <string>

namespace client {

namespace {

constexpr float kFadeSpeed = 6.0f;    // alpha units per second

int findEquipmentIndex(const GameStateMsg& gs, const ItemType& equip_type) {
    for (const auto& eq : gs.your_equipment) {
        if (eq.equip_type == equip_type) {
            return eq.inventory_index;
        }
    }
    return -1;
}

bool isEquippedIndex(const GameStateMsg& gs, int inv_idx) {
    for (const auto& eq : gs.your_equipment) {
        if (eq.inventory_index == inv_idx) return true;
    }
    return false;
}

std::optional<ItemType> equippedTypeForIndex(const GameStateMsg& gs, int inv_idx) {
    for (const auto& eq : gs.your_equipment) {
        if (eq.inventory_index == inv_idx) {
            return eq.equip_type;
        }
    }
    return std::nullopt;
}

} // namespace

float hudTotalHeight(float screen_h) {
    return 25.0f * widgetScale(screen_h);
}

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
             const std::function<std::optional<ItemType>(const std::string&)>& resolve_item_equip_type,
             HudOutput& out) {
    out.swap_msg.reset();
    out.drop_msg.reset();
    out.set_equipment_msg.reset();
    out.pickup_ground_item.reset();
    out.hotbar_activate = -1;

    const float screen_w = static_cast<float>(GetScreenWidth());
    const float screen_h = static_cast<float>(GetScreenHeight());

    // HP/MP bars — top-left corner (always full opacity)
    {
        const float bar_w = 180.0f;
        const float bar_h = 18.0f;
        const float bx = 12.0f;
        const float by = 12.0f;
        drawLabeledBar(ui_font, "HP", bx, by, bar_w, bar_h,
                       game_state.your_hp, game_state.your_max_hp,
                       115, 38, 38, 255);
        drawLabeledBar(ui_font, "MP", bx, by + bar_h + 4.0f, bar_w, bar_h,
                       game_state.your_mana, game_state.your_max_mana,
                       50, 80, 160, 255);
    }

    // Texture dimensions at integer pixel-art scale
    const float scale = widgetScale(screen_h) / 2;
    const float equip_tex_w = equip_tex.width * scale;
    const float equip_tex_h = equip_tex.height * scale;
    const float hotbar_tex_w = hotbar_tex.width * scale;
    const float hotbar_tex_h = hotbar_tex.height * scale;

    // Layout: equipment bar + gap + hotbar, centered at bottom
    const float row_w = equip_tex_w + kHudSectionGap + hotbar_tex_w;
    const float hud_h = std::max(equip_tex_h, hotbar_tex_h);
    const float hud_x = (screen_w - row_w) * 0.5f;
    const float hud_y = screen_h - hud_h - bottom_margin;
    const Rectangle hud_rect{hud_x, hud_y, row_w, hud_h};

    // Equipment bar position (left side, bottom-aligned)
    const float equip_x = hud_x;
    const float equip_y = hud_y + hud_h - equip_tex_h;
    const float equip_slot_w = equip_tex_w / kEquipSlotCount;

    // Hotbar position (right side, bottom-aligned)
    const float hotbar_x = hud_x + equip_tex_w + kHudSectionGap;
    const float hotbar_y = hud_y + hud_h - hotbar_tex_h;
    const float hotbar_slot_w = hotbar_tex_w / kHotbarSlots;

    // Hover detection — lerp alpha
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), hud_rect)
                         || state.drag.active
                         || dragging_ground_item_id >= 0
                         || overlay_visible;
    const float target_alpha = hovered ? kHudActiveAlpha : kHudIdleAlpha;
    if (state.hover_alpha < target_alpha) {
        state.hover_alpha = std::min(target_alpha, state.hover_alpha + kFadeSpeed * dt);
    } else {
        state.hover_alpha = std::max(target_alpha, state.hover_alpha - kFadeSpeed * dt);
    }
    const float a = state.hover_alpha;
    const auto icon_alpha = static_cast<unsigned char>(a * 255.0f);
    const Color icon_tint{255, 255, 255, icon_alpha};

    // Helper to find which equipment slot the point is in
    auto findEquipSlotAt = [&](const Vector2& p) -> int {
        const Rectangle equip_rect{equip_x, equip_y, equip_tex_w, equip_tex_h};
        if (!CheckCollisionPointRec(p, equip_rect)) return -1;
        const int slot = static_cast<int>((p.x - equip_x) / equip_slot_w);
        return (slot >= 0 && slot < kEquipSlotCount) ? slot : -1;
    };

    // Helper to find which hotbar slot the point is in
    auto findHotbarSlotAt = [&](const Vector2& p) -> int {
        const Rectangle hotbar_rect{hotbar_x, hotbar_y, hotbar_tex_w, hotbar_tex_h};
        if (!CheckCollisionPointRec(p, hotbar_rect)) return -1;
        const int slot = static_cast<int>((p.x - hotbar_x) / hotbar_slot_w);
        return (slot >= 0 && slot < kHotbarSlots) ? slot : -1;
    };

    // Auto-populate hotbar: slot i → inventory[i]
    for (int i = 0; i < kHotbarSlots; ++i) {
        state.hotbar_slots[i] = (i < static_cast<int>(game_state.inventory.size())) ? i : -1;
    }

    // --- Input handling ---

    // Left-click drag start on equipment slots
    if (!state.drag.active && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        const Vector2 m = GetMousePosition();
        const int eq_slot = findEquipSlotAt(m);
        if (eq_slot >= 0) {
            const int inv_idx = findEquipmentIndex(game_state, kEquipSlotTypes[eq_slot]);
            if (inv_idx >= 0 && inv_idx < static_cast<int>(game_state.inventory.size())
                && !game_state.inventory[inv_idx].empty()) {
                state.drag.active = true;
                state.drag.from_index = inv_idx;
                state.drag.item = game_state.inventory[inv_idx];
            }
        }
    }

    // Left-click drag start on hotbar (skip equipped items)
    if (!state.drag.active && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        const Vector2 m = GetMousePosition();
        const int slot = findHotbarSlotAt(m);
        if (slot >= 0) {
            const int inv_idx = state.hotbar_slots[slot];
            if (inv_idx >= 0 && inv_idx < static_cast<int>(game_state.inventory.size())
                && !game_state.inventory[inv_idx].empty()
                && !isEquippedIndex(game_state, inv_idx)) {
                state.drag.active = true;
                state.drag.from_index = inv_idx;
                state.drag.item = game_state.inventory[inv_idx];
            }
        }
    }

    // Right-click on equipment slot to unequip
    if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
        const Vector2 m = GetMousePosition();
        const int eq_slot = findEquipSlotAt(m);
        if (eq_slot >= 0) {
            const int inv_idx = findEquipmentIndex(game_state, kEquipSlotTypes[eq_slot]);
            if (inv_idx >= 0 && inv_idx < static_cast<int>(game_state.inventory.size())
                && !game_state.inventory[inv_idx].empty()) {
                out.set_equipment_msg = SetEquipmentMsg{kEquipSlotTypes[eq_slot], -1};
            }
        }
    }

    // Right-click on hotbar to equip
    if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
        const Vector2 m = GetMousePosition();
        const int slot = findHotbarSlotAt(m);
        if (slot >= 0) {
            const int inv_idx = state.hotbar_slots[slot];
            if (inv_idx >= 0 && inv_idx < static_cast<int>(game_state.inventory.size())
                && !game_state.inventory[inv_idx].empty()
                && !isEquippedIndex(game_state, inv_idx)) {
                const std::optional<ItemType> type = resolve_item_equip_type
                    ? resolve_item_equip_type(game_state.inventory[inv_idx])
                    : std::optional<ItemType>{};
                if (type.has_value()) {
                    out.set_equipment_msg = SetEquipmentMsg{*type, inv_idx};
                }
            }
        }
    }

    // Drag release — HUD runs AFTER overlay, so this handles everything the overlay didn't
    if (state.drag.active && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        const Vector2 m = GetMousePosition();

        const auto from_equip = equippedTypeForIndex(game_state, state.drag.from_index);
        const bool was_equipped = from_equip.has_value();

        const int eq_slot = findEquipSlotAt(m);
        const int hb_slot = findHotbarSlotAt(m);

        if (eq_slot >= 0 && !was_equipped) {
            // Drag TO equipment slot — equip if item type matches
            const std::optional<ItemType> item_type = resolve_item_equip_type
                ? resolve_item_equip_type(state.drag.item)
                : std::optional<ItemType>{};
            const ItemType slot_type = kEquipSlotTypes[eq_slot];
            if (item_type.has_value() && *item_type == slot_type) {
                out.set_equipment_msg = SetEquipmentMsg{*item_type, state.drag.from_index};
            }
        } else if (hb_slot >= 0) {
            // Drag to hotbar slot — server auto-unequips if source is equipped
            const int to_idx = hb_slot;
            if (to_idx != state.drag.from_index) {
                out.swap_msg = InventorySwapMsg{state.drag.from_index, to_idx};
            }
        } else if (!CheckCollisionPointRec(m, hud_rect)) {
            // Outside HUD
            if (was_equipped) {
                out.set_equipment_msg = SetEquipmentMsg{*from_equip, -1};
            } else {
                out.drop_msg = DropMsg{state.drag.from_index};
            }
        }
        // else: on HUD background — cancel

        state.drag = DragState{};
    }

    // --- Drawing (sprites only, no vector elements) ---

    // Equipment bar texture
    {
        const Rectangle src{0, 0, static_cast<float>(equip_tex.width), static_cast<float>(equip_tex.height)};
        const Rectangle dst{equip_x, equip_y, equip_tex_w, equip_tex_h};
        DrawTexturePro(equip_tex, src, dst, {0, 0}, 0.0f, Color{255, 255, 255, icon_alpha});
    }

    // Equipment item icons
    for (int i = 0; i < kEquipSlotCount; ++i) {
        const ItemType equip_type = kEquipSlotTypes[i];
        const int inv_idx = findEquipmentIndex(game_state, equip_type);
        const bool has_item = inv_idx >= 0 && inv_idx < static_cast<int>(game_state.inventory.size())
                              && !game_state.inventory[inv_idx].empty();

        if (has_item && (!state.drag.active || inv_idx != state.drag.from_index)) {
            const float sx = equip_x + i * equip_slot_w;
            const float inset = 2.0f * scale;
            const Rectangle icon_rect{sx + inset, equip_y + inset,
                                      equip_slot_w - inset * 2.0f, equip_tex_h - inset * 2.0f};
            if (draw_item_icon) draw_item_icon(game_state.inventory[inv_idx], icon_rect, icon_tint);
        }
    }

    // Hotbar texture
    {
        const Rectangle src{0, 0, static_cast<float>(hotbar_tex.width), static_cast<float>(hotbar_tex.height)};
        const Rectangle dst{hotbar_x, hotbar_y, hotbar_tex_w, hotbar_tex_h};
        DrawTexturePro(hotbar_tex, src, dst, {0, 0}, 0.0f, Color{255, 255, 255, icon_alpha});
    }

    // Hotbar item icons
    for (int i = 0; i < kHotbarSlots; ++i) {
        const int inv_idx = state.hotbar_slots[i];
        const bool has_item = inv_idx >= 0 && inv_idx < static_cast<int>(game_state.inventory.size())
                              && !game_state.inventory[inv_idx].empty()
                              && !isEquippedIndex(game_state, inv_idx);

        if (has_item && (!state.drag.active || inv_idx != state.drag.from_index)) {
            const float sx = hotbar_x + i * hotbar_slot_w;
            const float inset = 2.0f * scale;
            const Rectangle icon_rect{sx + inset, hotbar_y + inset,
                                      hotbar_slot_w - inset * 2.0f, hotbar_tex_h - inset * 2.0f};
            if (draw_item_icon) draw_item_icon(game_state.inventory[inv_idx], icon_rect, icon_tint);
        }
    }

    // Drag ghost (full opacity, always on top)
    if (state.drag.active) {
        const Vector2 m = GetMousePosition();
        Rectangle ghost{m.x - 22.0f, m.y - 18.0f, 44.0f, 34.0f};
        const Rectangle icon_rect{ghost.x + 6.0f, ghost.y + 4.0f, 60.0f, 48.0f};
        if (draw_item_icon) draw_item_icon(state.drag.item, icon_rect, WHITE);
    }

    // Ground item drag-to-HUD pickup
    if (dragging_ground_item_id >= 0 && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        if (CheckCollisionPointRec(GetMousePosition(), hud_rect)) {
            out.pickup_ground_item = PickupMsg{dragging_ground_item_id};
        }
    }

    // Hotbar key activation (1-8 = slots 0-7)
    for (int k = KEY_ONE; k <= KEY_EIGHT; ++k) {
        if (IsKeyPressed(k)) {
            out.hotbar_activate = k - KEY_ONE;
        }
    }
}

} // namespace client
