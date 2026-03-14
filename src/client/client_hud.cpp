#include "client_hud.h"
#include "client_game_ui.h"
#include "item_defs.h"

#include <algorithm>
#include <string>

namespace client {

namespace {

constexpr float kFadeSpeed = 6.0f;    // alpha units per second

// Find which inventory slot index has the given instance_id equipped for this type
int findEquippedSlotIndex(const GameStateMsg& gs, const ItemType& equip_type) {
    for (const auto& eq : gs.your_equipment) {
        if (eq.equip_type == equip_type && eq.instance_id > 0) {
            for (int i = 0; i < static_cast<int>(gs.inventory.size()); ++i) {
                if (gs.inventory[i].instance_id == eq.instance_id) return i;
            }
        }
    }
    return -1;
}

// Find the instance_id equipped for a given type, or 0
int64_t findEquippedInstanceId(const GameStateMsg& gs, const ItemType& equip_type) {
    for (const auto& eq : gs.your_equipment) {
        if (eq.equip_type == equip_type) return eq.instance_id;
    }
    return 0;
}

// Check if this instance_id is equipped anywhere
bool isEquippedInstance(const GameStateMsg& gs, int64_t instance_id) {
    if (instance_id <= 0) return false;
    for (const auto& eq : gs.your_equipment) {
        if (eq.instance_id == instance_id) return true;
    }
    return false;
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
             const ContainerDef& hotbar_def,
             const ContainerDef& equip_def,
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

    const int equip_slot_count = static_cast<int>(equip_def.slots.size());
    const int hotbar_slot_count = static_cast<int>(hotbar_def.slots.size());

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

    // Hotbar position (right side, bottom-aligned)
    const float hotbar_x = hud_x + equip_tex_w + kHudSectionGap;
    const float hotbar_y = hud_y + hud_h - hotbar_tex_h;

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

    // Helper to compute scaled rect for an equip slot
    auto equipSlotRect = [&](int i) -> Rectangle {
        if (i < 0 || i >= equip_slot_count) return {};
        const auto& s = equip_def.slots[i];
        return {equip_x + s.x * scale, equip_y + s.y * scale, s.w * scale, s.h * scale};
    };

    // Helper to compute scaled rect for a hotbar slot
    auto hotbarSlotRect = [&](int i) -> Rectangle {
        if (i < 0 || i >= hotbar_slot_count) return {};
        const auto& s = hotbar_def.slots[i];
        return {hotbar_x + s.x * scale, hotbar_y + s.y * scale, s.w * scale, s.h * scale};
    };

    // Helper to find which equipment slot the point is in
    auto findEquipSlotAt = [&](const Vector2& p) -> int {
        for (int i = 0; i < equip_slot_count; ++i) {
            if (CheckCollisionPointRec(p, equipSlotRect(i))) return i;
        }
        return -1;
    };

    // Helper to find which hotbar slot the point is in
    auto findHotbarSlotAt = [&](const Vector2& p) -> int {
        for (int i = 0; i < hotbar_slot_count; ++i) {
            if (CheckCollisionPointRec(p, hotbarSlotRect(i))) return i;
        }
        return -1;
    };

    // Get equipment type for a slot from container def
    auto equipSlotType = [&](int i) -> std::optional<ItemType> {
        if (i < 0 || i >= equip_slot_count) return std::nullopt;
        return equip_def.slots[i].type_constraint;
    };

    // Auto-populate hotbar: slot i -> inventory[i]
    state.hotbar_slots.resize(hotbar_slot_count, -1);
    for (int i = 0; i < hotbar_slot_count; ++i) {
        state.hotbar_slots[i] = (i < static_cast<int>(game_state.inventory.size())) ? i : -1;
    }

    // --- Input handling ---

    // Left-click drag start on equipment slots
    if (!state.drag.active && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        const Vector2 m = GetMousePosition();
        const int eq_slot = findEquipSlotAt(m);
        if (eq_slot >= 0) {
            const auto slot_type = equipSlotType(eq_slot);
            if (slot_type.has_value()) {
                const int inv_idx = findEquippedSlotIndex(game_state, *slot_type);
                if (inv_idx >= 0 && inv_idx < static_cast<int>(game_state.inventory.size())
                    && game_state.inventory[inv_idx].instance_id > 0) {
                    state.drag.active = true;
                    state.drag.from_index = inv_idx;
                    state.drag.instance_id = game_state.inventory[inv_idx].instance_id;
                    state.drag.item = game_state.inventory[inv_idx].display_name;
                    state.drag.def_id = game_state.inventory[inv_idx].def_id;
                }
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
                && game_state.inventory[inv_idx].instance_id > 0
                && !isEquippedInstance(game_state, game_state.inventory[inv_idx].instance_id)) {
                state.drag.active = true;
                state.drag.from_index = inv_idx;
                state.drag.instance_id = game_state.inventory[inv_idx].instance_id;
                state.drag.item = game_state.inventory[inv_idx].display_name;
                state.drag.def_id = game_state.inventory[inv_idx].def_id;
            }
        }
    }

    // Right-click on equipment slot to unequip
    if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
        const Vector2 m = GetMousePosition();
        const int eq_slot = findEquipSlotAt(m);
        if (eq_slot >= 0) {
            const auto slot_type = equipSlotType(eq_slot);
            if (slot_type.has_value()) {
                const int64_t iid = findEquippedInstanceId(game_state, *slot_type);
                if (iid > 0) {
                    out.set_equipment_msg = SetEquipmentMsg{*slot_type, 0};
                }
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
                && game_state.inventory[inv_idx].instance_id > 0
                && !isEquippedInstance(game_state, game_state.inventory[inv_idx].instance_id)) {
                const auto& slot_data = game_state.inventory[inv_idx];
                const std::optional<ItemType> type = resolve_item_equip_type
                    ? resolve_item_equip_type(slot_data.def_id)
                    : std::optional<ItemType>{};
                if (type.has_value()) {
                    out.set_equipment_msg = SetEquipmentMsg{*type, slot_data.instance_id};
                }
            }
        }
    }

    // Drag release — HUD runs AFTER overlay, so this handles everything the overlay didn't
    if (state.drag.active && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        const Vector2 m = GetMousePosition();

        const bool was_equipped = isEquippedInstance(game_state, state.drag.instance_id);

        const int eq_slot = findEquipSlotAt(m);
        const int hb_slot = findHotbarSlotAt(m);

        if (eq_slot >= 0 && !was_equipped) {
            // Drag TO equipment slot — equip if item type matches
            const std::optional<ItemType> item_type = resolve_item_equip_type
                ? resolve_item_equip_type(state.drag.def_id)
                : std::optional<ItemType>{};
            const auto slot_type = equipSlotType(eq_slot);
            if (item_type.has_value() && slot_type.has_value() && *item_type == *slot_type) {
                out.set_equipment_msg = SetEquipmentMsg{*item_type, state.drag.instance_id};
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
                // Find the actual equip type for this instance and unequip it
                for (const auto& eq : game_state.your_equipment) {
                    if (eq.instance_id == state.drag.instance_id) {
                        out.set_equipment_msg = SetEquipmentMsg{eq.equip_type, 0};
                        break;
                    }
                }
            } else {
                out.drop_msg = DropMsg{state.drag.instance_id};
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
    for (int i = 0; i < equip_slot_count; ++i) {
        const auto slot_type = equipSlotType(i);
        if (!slot_type.has_value()) continue;
        const int inv_idx = findEquippedSlotIndex(game_state, *slot_type);
        const bool has_item = inv_idx >= 0 && inv_idx < static_cast<int>(game_state.inventory.size())
                              && game_state.inventory[inv_idx].instance_id > 0;

        if (has_item && (!state.drag.active || inv_idx != state.drag.from_index)) {
            const Rectangle sr = equipSlotRect(i);
            const float inset = 2.0f * scale;
            const Rectangle icon_rect{sr.x + inset, sr.y + inset,
                                      sr.width - inset * 2.0f, sr.height - inset * 2.0f};
            if (draw_item_icon) draw_item_icon(game_state.inventory[inv_idx].def_id, icon_rect, icon_tint);
        }
    }

    // Hotbar texture
    {
        const Rectangle src{0, 0, static_cast<float>(hotbar_tex.width), static_cast<float>(hotbar_tex.height)};
        const Rectangle dst{hotbar_x, hotbar_y, hotbar_tex_w, hotbar_tex_h};
        DrawTexturePro(hotbar_tex, src, dst, {0, 0}, 0.0f, Color{255, 255, 255, icon_alpha});
    }

    // Hotbar item icons
    for (int i = 0; i < hotbar_slot_count; ++i) {
        const int inv_idx = state.hotbar_slots[i];
        const bool has_item = inv_idx >= 0 && inv_idx < static_cast<int>(game_state.inventory.size())
                              && game_state.inventory[inv_idx].instance_id > 0
                              && !isEquippedInstance(game_state, game_state.inventory[inv_idx].instance_id);

        if (has_item && (!state.drag.active || inv_idx != state.drag.from_index)) {
            const Rectangle sr = hotbarSlotRect(i);
            const float inset = 2.0f * scale;
            const Rectangle icon_rect{sr.x + inset, sr.y + inset,
                                      sr.width - inset * 2.0f, sr.height - inset * 2.0f};
            if (draw_item_icon) draw_item_icon(game_state.inventory[inv_idx].def_id, icon_rect, icon_tint);
        }
    }

    // Drag ghost (full opacity, always on top)
    if (state.drag.active) {
        const Vector2 m = GetMousePosition();
        Rectangle ghost{m.x - 22.0f, m.y - 18.0f, 44.0f, 34.0f};
        const Rectangle icon_rect{ghost.x + 6.0f, ghost.y + 4.0f, 60.0f, 48.0f};
        if (draw_item_icon) draw_item_icon(state.drag.def_id, icon_rect, WHITE);
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
