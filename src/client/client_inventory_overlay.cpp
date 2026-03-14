#include "client_inventory_overlay.h"
#include "client_ui_primitives.h"
#include "client_game_ui.h"

#include <algorithm>
#include <string>

namespace client {

namespace {

constexpr Color kOverlayBg{0, 0, 0, 160};
constexpr Color kPanelBg{24, 28, 32, 240};
constexpr Color kPanelBorder{96, 109, 124, 255};
constexpr Color kSlotBg{37, 42, 48, 230};
constexpr Color kSlotBorder{90, 102, 117, 255};
constexpr Color kSectionTitle{220, 225, 235, 255};

bool isEquippedIndex(const GameStateMsg& gs, int inv_idx) {
    for (const auto& eq : gs.your_equipment) {
        if (eq.inventory_index == inv_idx) return true;
    }
    return false;
}

} // namespace

void tickInventoryOverlay(InventoryOverlayState& state, float dt) {
    if (state.visible) {
        state.alpha = std::min(1.0f, state.alpha + dt / kOverlayFadeDuration);
    } else {
        state.alpha = std::max(0.0f, state.alpha - dt / kOverlayFadeDuration);
    }
}

void drawInventoryOverlay(Font ui_font,
                          const GameStateMsg& game_state,
                          InventoryOverlayState& state,
                          DragState& drag,
                          const std::function<bool(const std::string&, const Rectangle&, Color)>& draw_item_icon,
                          const std::function<std::optional<ItemType>(const std::string&)>& resolve_item_equip_type,
                          InventoryOverlayOutput& out) {
    out.swap_msg.reset();
    out.drop_msg.reset();
    out.set_equipment_msg.reset();

    if (state.alpha <= 0.0f) return;

    const float screen_w = static_cast<float>(GetScreenWidth());
    const float screen_h = static_cast<float>(GetScreenHeight());
    const unsigned char alpha = static_cast<unsigned char>(state.alpha * 255.0f);

    // Dim background
    DrawRectangle(0, 0, static_cast<int>(screen_w), static_cast<int>(screen_h),
                  Color{kOverlayBg.r, kOverlayBg.g, kOverlayBg.b, static_cast<unsigned char>(kOverlayBg.a * state.alpha)});

    // Panel dimensions
    const float grid_w = kBackpackCols * kOverlaySlotSize + (kBackpackCols - 1) * kOverlaySlotGap;
    const float grid_h = kBackpackRows * kOverlaySlotSize + (kBackpackRows - 1) * kOverlaySlotGap;
    const float skills_section_h = 180.0f;
    const float panel_pad = 16.0f;
    const float section_gap = 12.0f;

    const float panel_w = grid_w + panel_pad * 2.0f + 20.0f;
    const float panel_h = panel_pad + 20.0f + grid_h + section_gap +
                          20.0f + skills_section_h + panel_pad;
    const float panel_x = (screen_w - panel_w) * 0.5f;
    const float panel_y = (screen_h - panel_h) * 0.5f;

    // Panel background
    const Rectangle panel_rect{panel_x, panel_y, panel_w, panel_h};
    drawUiPanel(panel_rect,
                Color{kPanelBg.r, kPanelBg.g, kPanelBg.b, alpha},
                Color{kPanelBorder.r, kPanelBorder.g, kPanelBorder.b, alpha},
                2.0f);

    float cy = panel_y + panel_pad;

    // === Backpack Section ===
    drawUiText(ui_font, "Backpack", panel_x + panel_pad, cy, 16.0f,
               Color{kSectionTitle.r, kSectionTitle.g, kSectionTitle.b, alpha});
    cy += 20.0f;

    const float bp_start_x = panel_x + (panel_w - grid_w) * 0.5f;
    const float bp_start_y = cy;
    const int inv_count = static_cast<int>(game_state.inventory.size());

    auto findBackpackSlotAt = [&](const Vector2& p) -> int {
        for (int i = 0; i < kBackpackSlots; ++i) {
            const int c = i % kBackpackCols;
            const int r = i / kBackpackCols;
            Rectangle slot{
                bp_start_x + c * (kOverlaySlotSize + kOverlaySlotGap),
                bp_start_y + r * (kOverlaySlotSize + kOverlaySlotGap),
                kOverlaySlotSize,
                kOverlaySlotSize
            };
            if (CheckCollisionPointRec(p, slot)) return i;
        }
        return -1;
    };

    // Convert backpack slot to inventory index
    auto bpToInv = [](int bp_slot) -> int {
        return kBackpackIndexOffset + bp_slot;
    };

    // Check if an inventory index has a valid, non-empty, non-equipped item
    auto hasItemAt = [&](int inv_idx) -> bool {
        return inv_idx >= 0 && inv_idx < inv_count
               && !game_state.inventory[inv_idx].empty()
               && !isEquippedIndex(game_state, inv_idx);
    };

    // Drag start from backpack
    if (state.alpha >= 1.0f && !drag.active && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        const Vector2 m = GetMousePosition();
        const int slot = findBackpackSlotAt(m);
        if (slot >= 0) {
            const int inv_idx = bpToInv(slot);
            if (hasItemAt(inv_idx)) {
                drag.active = true;
                drag.from_index = inv_idx;
                drag.item = game_state.inventory[inv_idx];
            }
        }
    }

    // Right-click to equip from backpack
    if (state.alpha >= 1.0f && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
        const Vector2 m = GetMousePosition();
        const int slot = findBackpackSlotAt(m);
        if (slot >= 0) {
            const int inv_idx = bpToInv(slot);
            if (hasItemAt(inv_idx)) {
                const std::optional<ItemType> type = resolve_item_equip_type
                    ? resolve_item_equip_type(game_state.inventory[inv_idx])
                    : std::optional<ItemType>{};
                if (type.has_value()) {
                    out.set_equipment_msg = SetEquipmentMsg{*type, inv_idx};
                }
            }
        }
    }

    // Drag release: swap to backpack slot, cancel on panel bg, or leave active for HUD
    if (drag.active && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        const Vector2 m = GetMousePosition();
        const int to_slot = findBackpackSlotAt(m);
        if (to_slot >= 0) {
            const int to_inv = bpToInv(to_slot);
            if (to_inv != drag.from_index) {
                // Server auto-unequips if source is equipped, just send the swap
                out.swap_msg = InventorySwapMsg{drag.from_index, to_inv};
            }
            drag = DragState{};
        } else if (CheckCollisionPointRec(m, panel_rect)) {
            // Dropped on panel background — cancel
            drag = DragState{};
        }
        // Outside panel: leave drag active for HUD to handle
    }

    // Draw backpack slots
    for (int i = 0; i < kBackpackSlots; ++i) {
        const int c = i % kBackpackCols;
        const int r = i / kBackpackCols;
        Rectangle slot{
            bp_start_x + c * (kOverlaySlotSize + kOverlaySlotGap),
            bp_start_y + r * (kOverlaySlotSize + kOverlaySlotGap),
            kOverlaySlotSize,
            kOverlaySlotSize
        };

        const int inv_idx = bpToInv(i);

        drawUiPanel(slot,
                    Color{kSlotBg.r, kSlotBg.g, kSlotBg.b, alpha},
                    Color{kSlotBorder.r, kSlotBorder.g, kSlotBorder.b, alpha},
                    1.0f);

        if (hasItemAt(inv_idx) && (!drag.active || inv_idx != drag.from_index)) {
            const Rectangle icon_rect{slot.x + 4.0f, slot.y + 4.0f, slot.width - 8.0f, slot.height - 8.0f};
            if (draw_item_icon) draw_item_icon(game_state.inventory[inv_idx], icon_rect, Color{255, 255, 255, alpha});
        }
    }

    cy += grid_h + section_gap;

    // === Skills Section ===
    drawUiText(ui_font, "Skills", panel_x + panel_pad, cy, 16.0f,
               Color{kSectionTitle.r, kSectionTitle.g, kSectionTitle.b, alpha});
    cy += 20.0f;

    const float bar_w = std::max(80.0f, panel_w - panel_pad * 2.0f - 30.0f);
    const float bx = panel_x + panel_pad;
    float by = cy;
    const float level_w = 24.0f;
    const float row_h = 16.0f;
    const float row_gap = 20.0f;
    const float prog_x = bx + level_w;
    const float prog_w = std::max(50.0f, bar_w - level_w);
    const float label_size = 14.0f;

    auto draw_skill = [&](const char* name,
                          int level,
                          int xp,
                          int xp_to_next,
                          unsigned char r,
                          unsigned char g,
                          unsigned char b) {
        drawUiText(ui_font,
                   std::to_string(std::max(1, level)),
                   bx,
                   by + 1.0f,
                   label_size,
                   Color{220, 225, 235, alpha});
        drawLabeledBar(ui_font,
                       name,
                       prog_x,
                       by,
                       prog_w,
                       row_h,
                       xp,
                       std::max(1, xp_to_next),
                       r, g, b, alpha);
        by += row_gap;
    };

    draw_skill("EXP", game_state.your_level, game_state.your_exp, std::max(1, game_state.your_exp_to_next_level), 153, 121, 60);
    draw_skill("Hit", game_state.skill_melee_level, game_state.skill_melee_xp, game_state.skill_melee_xp_to_next, 128, 88, 42);
    draw_skill("Block", game_state.skill_shielding_level, game_state.skill_shielding_xp, game_state.skill_shielding_xp_to_next, 72, 104, 144);
    draw_skill("Evade", game_state.skill_evasion_level, game_state.skill_evasion_xp, game_state.skill_evasion_xp_to_next, 112, 112, 112);
    draw_skill("Distance", game_state.skill_distance_level, game_state.skill_distance_xp, game_state.skill_distance_xp_to_next, 80, 126, 80);
    draw_skill("Magic", game_state.skill_magic_level, game_state.skill_magic_xp, game_state.skill_magic_xp_to_next, 82, 72, 153);

    // Stat summary
    const float txt_size = 14.0f;
    const float y1 = by + 4.0f;
    drawUiText(ui_font, "Attack: " + std::to_string(game_state.trait_attack), bx, y1, txt_size,
               Color{255, 205, 164, alpha});
    drawUiText(ui_font, "Shielding: " + std::to_string(game_state.trait_shielding), bx, y1 + 14.0f, txt_size,
               Color{190, 218, 255, alpha});
    drawUiText(ui_font, "Evasion: " + std::to_string(game_state.trait_evasion), bx, y1 + 28.0f, txt_size,
               Color{225, 225, 225, alpha});
    drawUiText(ui_font, "Armor: " + std::to_string(game_state.trait_armor), bx, y1 + 42.0f, txt_size,
               Color{203, 219, 177, alpha});
}

} // namespace client
