#pragma once

#include "client_support.h"
#include "msg.h"
#include "room.h"
#include "sprites.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <functional>

namespace client {

struct SceneState {
    std::unordered_map<std::string, AnimationComponent> anim_by_key;
    std::unordered_map<std::string, std::pair<int, int>> prev_pos_by_key;
    std::unordered_map<std::string, std::pair<float, float>> render_pos_by_key;
    int dragging_ground_item_id = -1;
    float attack_fx_t = 0.0f;
};

struct SceneConfig {
    float map_scale = 2.0f;
    float inventory_panel_width = 260.0f;
    float bottom_panel_height = 180.0f;
    float player_slide_tiles_per_sec = 10.0f;
};

struct SceneOutput {
    std::optional<AttackMsg> attack_click;
    std::optional<MoveGroundItemMsg> move_ground_item;
};

struct SpriteSheetView {
    const Sprites* sprites = nullptr;
    Texture2D texture{};
    bool ready = false;
};

using ItemSheetView = SpriteSheetView;

void drawScene(const Room& room,
               const GameStateMsg& game_state,
               const Sprites& sprites,
               Texture2D character_tex,
               bool sprite_ready,
               const std::function<SpriteSheetView(const std::string&)>& monster_sheet_view,
               const std::function<ItemSheetView(const std::string&)>& item_sheet_view,
               Font ui_font,
               float dt,
               SceneState& scene_state,
               const SceneConfig& cfg,
               SceneOutput& out);

} // namespace client
