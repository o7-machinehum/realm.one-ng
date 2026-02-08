#include "client_inventory_ui.h"

#include <algorithm>

namespace client {

void drawInventoryUi(Font ui_font,
                     const GameStateMsg& game_state,
                     InventoryUiState& state,
                     const InventoryUiConfig& cfg,
                     InventoryUiOutput& out) {
    out.swap_msg.reset();
    out.drop_msg.reset();

    Rectangle inv_panel{
        static_cast<float>(GetScreenWidth()) - cfg.panel_w,
        0.0f,
        cfg.panel_w,
        static_cast<float>(GetScreenHeight() - cfg.bottom_panel_height)
    };
    DrawRectangleRec(inv_panel, Fade(BLACK, 0.78f));
    DrawRectangleLinesEx(inv_panel, 1.0f, GRAY);
    drawUiText(ui_font, "Inventory", inv_panel.x + 12.0f, inv_panel.y + 12.0f, 16, WHITE);

    const int inv_count = static_cast<int>(game_state.inventory.size());
    const int max_slots = std::max(20, inv_count + 4);
    const float start_x = inv_panel.x + 16.0f;
    const float start_y = inv_panel.y + 50.0f;

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        const Vector2 m = GetMousePosition();
        const int slot = slotAtPoint(inv_panel, m, cfg.cols, cfg.slot_size, cfg.gap, max_slots);
        if (slot >= 0 && slot < inv_count) {
            state.drag.active = true;
            state.drag.from_index = slot;
            state.drag.item = game_state.inventory[slot];
        }
    }

    if (state.drag.active && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        const Vector2 m = GetMousePosition();
        const int to_slot = slotAtPoint(inv_panel, m, cfg.cols, cfg.slot_size, cfg.gap, max_slots);
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
            start_x + c * (cfg.slot_size + cfg.gap),
            start_y + r * (cfg.slot_size + cfg.gap),
            cfg.slot_size,
            cfg.slot_size
        };
        DrawRectangleRec(slot, Fade(DARKGRAY, 0.7f));
        DrawRectangleLinesEx(slot, 1.0f, GRAY);
        drawUiText(ui_font, std::to_string(i), slot.x + 3.0f, slot.y + 2.0f, 10, LIGHTGRAY);

        if (i < inv_count && (!state.drag.active || i != state.drag.from_index)) {
            DrawRectangle(static_cast<int>(slot.x + 6), static_cast<int>(slot.y + 16), static_cast<int>(slot.width - 12), 14, GOLD);
            std::string label = game_state.inventory[i];
            if (label.size() > 10) label = label.substr(0, 10);
            drawUiText(ui_font, label, slot.x + 4.0f, slot.y + 32.0f, 11, WHITE);
        }
    }

    if (state.drag.active) {
        const Vector2 m = GetMousePosition();
        Rectangle ghost{m.x - 24.0f, m.y - 20.0f, 48.0f, 40.0f};
        DrawRectangleRec(ghost, Fade(GOLD, 0.65f));
        DrawRectangleLinesEx(ghost, 1.0f, YELLOW);
        std::string label = state.drag.item;
        if (label.size() > 10) label = label.substr(0, 10);
        drawUiText(ui_font, label, ghost.x + 4.0f, ghost.y + 14.0f, 11, BLACK);
    }
}

} // namespace client
