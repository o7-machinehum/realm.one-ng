#include "client_inventory_ui.h"

#include <algorithm>

namespace client {

namespace {

Rectangle inventoryPanelRect(const InventoryUiConfig& cfg) {
    return Rectangle{
        static_cast<float>(GetScreenWidth()) - cfg.panel_w,
        0.0f,
        cfg.panel_w,
        static_cast<float>(GetScreenHeight() - cfg.bottom_panel_height)
    };
}

bool isEquipType(const std::string& t) {
    return t == "Weapon" || t == "Armor" || t == "Shield" ||
           t == "Legs" || t == "Boots" || t == "Helmet";
}

} // namespace

void drawInventoryUi(Font ui_font,
                     const GameStateMsg& game_state,
                     InventoryUiState& state,
                     const InventoryUiConfig& cfg,
                     const std::function<bool(const std::string&, const Rectangle&)>& draw_item_icon,
                     const std::function<std::string(const std::string&)>& resolve_item_equip_type,
                     InventoryUiOutput& out) {
    out.swap_msg.reset();
    out.drop_msg.reset();
    out.set_equipment_msg.reset();

    Rectangle inv_panel = inventoryPanelRect(cfg);
    Rectangle title_bar{inv_panel.x, inv_panel.y, inv_panel.width, 32.0f};

    DrawRectangleRec(inv_panel, Color{26, 30, 34, 235});
    DrawRectangleRec(title_bar, Color{42, 51, 60, 255});
    DrawRectangleLinesEx(inv_panel, 1.0f, Color{98, 111, 126, 255});
    drawUiText(ui_font, "Inventory", inv_panel.x + 10.0f, inv_panel.y + 7.0f, 16, RAYWHITE);

    const int inv_count = static_cast<int>(game_state.inventory.size());
    const int max_slots = std::max(24, inv_count + 6);

    const float inv_start_x = inv_panel.x + 12.0f;
    const float inv_start_y = inv_panel.y + 42.0f;

    auto findInventorySlotAt = [&](const Vector2& p) -> int {
        for (int i = 0; i < max_slots; ++i) {
            const int c = i % cfg.cols;
            const int r = i / cfg.cols;
            Rectangle slot{
                inv_start_x + c * (cfg.slot_size + cfg.gap),
                inv_start_y + r * (cfg.slot_size + cfg.gap),
                cfg.slot_size,
                cfg.slot_size
            };
            if (CheckCollisionPointRec(p, slot)) return i;
        }
        return -1;
    };

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        const Vector2 m = GetMousePosition();
        const int slot = findInventorySlotAt(m);
        if (slot >= 0 && slot < inv_count) {
            state.drag.active = true;
            state.drag.from_index = slot;
            state.drag.item = game_state.inventory[slot];
        }
    }

    if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
        const Vector2 m = GetMousePosition();
        const int slot = findInventorySlotAt(m);
        if (slot >= 0 && slot < inv_count) {
            const std::string type = resolve_item_equip_type ? resolve_item_equip_type(game_state.inventory[slot]) : std::string{};
            if (isEquipType(type)) {
                bool already_equipped = false;
                for (const auto& eq : game_state.your_equipment) {
                    if (eq.equip_type != type) continue;
                    if (eq.inventory_index == slot) {
                        already_equipped = true;
                    }
                    break;
                }
                out.set_equipment_msg = SetEquipmentMsg{type, already_equipped ? -1 : slot};
            }
        }
    }

    if (state.drag.active && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        const Vector2 m = GetMousePosition();
        const int to_slot = findInventorySlotAt(m);
        if (to_slot >= 0 && to_slot < inv_count && to_slot != state.drag.from_index) {
            out.swap_msg = InventorySwapMsg{state.drag.from_index, to_slot};
        } else if (m.x < inv_panel.x) {
            out.drop_msg = DropMsg{state.drag.from_index};
        }
        state.drag = DragState{};
    }

    for (int i = 0; i < max_slots; ++i) {
        const int c = i % cfg.cols;
        const int r = i / cfg.cols;
        Rectangle slot{
            inv_start_x + c * (cfg.slot_size + cfg.gap),
            inv_start_y + r * (cfg.slot_size + cfg.gap),
            cfg.slot_size,
            cfg.slot_size
        };
        DrawRectangleRec(slot, Color{37, 42, 48, 230});

        bool equipped = false;
        if (i < inv_count) {
            const std::string type = resolve_item_equip_type ? resolve_item_equip_type(game_state.inventory[i]) : std::string{};
            for (const auto& eq : game_state.your_equipment) {
                if (eq.equip_type == type && eq.inventory_index == i) {
                    equipped = true;
                    break;
                }
            }
        }
        DrawRectangleLinesEx(slot, equipped ? 2.0f : 1.0f, equipped ? YELLOW : Color{90, 102, 117, 255});

        if (i < inv_count && (!state.drag.active || i != state.drag.from_index)) {
            const Rectangle icon_rect{slot.x + 4.0f, slot.y + 4.0f, slot.width - 8.0f, slot.height - 8.0f};
            if (!draw_item_icon || !draw_item_icon(game_state.inventory[i], icon_rect)) {
                DrawRectangle(static_cast<int>(icon_rect.x),
                              static_cast<int>(icon_rect.y),
                              static_cast<int>(icon_rect.width),
                              static_cast<int>(icon_rect.height),
                              Color{160, 128, 80, 255});
            }
        }
    }

    if (state.drag.active) {
        const Vector2 m = GetMousePosition();
        Rectangle ghost{m.x - 22.0f, m.y - 18.0f, 44.0f, 34.0f};
        DrawRectangleRec(ghost, Color{216, 176, 82, 190});
        DrawRectangleLinesEx(ghost, 1.0f, Color{248, 216, 140, 255});
        const Rectangle icon_rect{ghost.x + 6.0f, ghost.y + 4.0f, 30.0f, 24.0f};
        if (!draw_item_icon || !draw_item_icon(state.drag.item, icon_rect)) {
            DrawRectangle(static_cast<int>(icon_rect.x),
                          static_cast<int>(icon_rect.y),
                          static_cast<int>(icon_rect.width),
                          static_cast<int>(icon_rect.height),
                          Color{110, 140, 96, 255});
        }
    }
}

} // namespace client
