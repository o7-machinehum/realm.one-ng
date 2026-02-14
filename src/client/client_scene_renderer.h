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
    std::unordered_map<std::string, uint32_t> last_attack_seq_by_key;
    std::unordered_map<std::string, float> attack_fx_timer_by_key;
    std::unordered_map<std::string, std::string> speech_text_by_user;
    std::unordered_map<std::string, std::string> speech_type_by_user;
    std::unordered_map<std::string, float> speech_timer_by_user;
    std::unordered_map<std::string, uint64_t> speech_seq_by_user;
    uint64_t speech_seq_counter = 0;
    int dragging_ground_item_id = -1;
    float attack_fx_t = 0.0f;
};

struct SceneConfig {
    float map_scale = 2.0f;
    float map_origin_x = 0.0f;
    float map_origin_y = 0.0f;
    float map_view_width = 0.0f;
    float map_view_height = 0.0f;
    float inventory_panel_width = 260.0f;
    float inventory_top_offset = 72.0f;
    Rectangle inventory_drop_rect{0, 0, 0, 0};
    float bottom_panel_height = 180.0f;
    bool inventory_visible = true;
    float player_slide_tiles_per_sec = 10.0f;
    float monster_slide_tiles_per_sec = 8.0f;
    float speech_bubble_alpha = 0.5f;
    float player_name_text_size = 13.0f;
    float monster_name_text_size = 16.0f;
    float npc_name_text_size = 13.0f;
    float speech_text_size = 16.0f;
};

struct SceneOutput {
    std::optional<AttackMsg> attack_click;
    std::optional<MoveGroundItemMsg> move_ground_item;
    std::optional<PickupMsg> pickup_ground_item;
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
               Texture2D speech_tex,
               bool speech_ready,
               const std::function<SpriteSheetView(const std::string&)>& monster_sheet_view,
               const std::function<ItemSheetView(const std::string&)>& item_sheet_view,
               Font ui_font,
               float dt,
               SceneState& scene_state,
               const SceneConfig& cfg,
               SceneOutput& out);

void drawSpeechOverlays(const Room& room,
                        const GameStateMsg& game_state,
                        Texture2D speech_tex,
                        bool speech_ready,
                        Font ui_font,
                        SceneState& scene_state,
                        const SceneConfig& cfg);

} // namespace client
