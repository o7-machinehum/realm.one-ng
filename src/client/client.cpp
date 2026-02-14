#include "raylib.h"

#include "client_support.h"
#include "client_ui_primitives.h"
#include "client_game_ui.h"
#include "client_inventory_ui.h"
#include "client_layout.h"
#include "client_scene_renderer.h"
#include "client_sheet_cache.h"
#include "ui_settings.h"
#include "client_windows.h"
#include "auth_ui.h"
#include "msg.h"
#include "monster_defs.h"
#include "net_client.h"
#include "room.h"
#include "room_render.h"
#include "sprites.h"

#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace {
constexpr int kInitialWindowW = 1200;
constexpr int kInitialWindowH = 760;
constexpr int kMaxInputChars = 180;
constexpr int kGameplayBottomPanelPx = 0;
constexpr float kPlayViewportMaxWidthRatio = 0.92f;
constexpr float kPlayViewportMaxHeightRatio = 0.88f;
constexpr float kDockMarginTop = 10.0f;
constexpr float kDockMarginRight = 10.0f;
constexpr float kDockWindowGapY = 8.0f;
constexpr float kInputOverlayHeight = 34.0f;
constexpr float kInputOverlayMargin = 8.0f;
constexpr float kInputTextOffsetX = 8.0f;
constexpr float kInputTextOffsetY = 6.0f;
constexpr float kInputFontSize = 19.0f;

// Parses raw chat input into a typed speech message.
bool parseSpeechInput(const std::string& input, ChatMsg& out) {
    std::istringstream iss(input);
    std::string cmd;
    iss >> cmd;

    if (cmd == "/yell" || cmd == "!") out.speech_type = "yell";
    else if (cmd == "/think" || cmd == ".") out.speech_type = "think";
    else if (cmd == "/talk" || cmd == "/say") out.speech_type = "talk";
    else if (!input.empty() && input.front() != '/') out.speech_type = "talk";
    else return false;

    if (cmd == "/yell" || cmd == "/think" || cmd == "/talk" || cmd == "/say") {
        std::string rest;
        std::getline(iss, rest);
        if (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
        out.text = rest;
    } else {
        out.text = input;
    }
    out.from.clear();
    return !out.text.empty();
}

} // namespace

// Owns the client runtime: input, networking mailbox pump, and frame rendering.
int main(int argc, char** argv) {
    InitWindow(kInitialWindowW, kInitialWindowH, "The Island");
    SetTargetFPS(60);

    std::string host = "127.0.0.1";
    uint16_t port = 7000;
    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = static_cast<uint16_t>(std::stoi(argv[2]));

    Mailbox mailbox;
    RoomRenderer rr;
    NetClient nc(mailbox, host, port);
    nc.start();

    Sprites sprites;
    Texture2D character_tex{};
    Texture2D combat_fx_tex{};
    Texture2D speech_tex{};
    bool sprite_ready = false;
    bool combat_fx_ready = false;
    bool speech_ready = false;
    Sprites::SizeOverrideMap player_size_overrides;
    player_size_overrides["player_1"] = {1, 2};
    if (sprites.loadTSX("game/assets/art/character.tsx", player_size_overrides)) {
        const std::string tex_path = "game/assets/art/" + sprites.image_source();
        character_tex = LoadTexture(tex_path.c_str());
        sprite_ready = character_tex.id != 0;
    }
    bool owns_ui_font = false;
    Font ui_font = client::loadUIFont(owns_ui_font);
    speech_tex = LoadTexture("game/assets/art/speech.png");
    speech_ready = (speech_tex.id != 0);
    combat_fx_tex = LoadTexture("game/assets/art/properties.png");
    combat_fx_ready = (combat_fx_tex.id != 0);

    std::optional<Room> current_room;
    std::optional<GameStateMsg> game_state;
    std::string last_game_state_room;
    std::deque<std::string> logs;
    std::string input;
    std::string last_event;
    client::SceneState scene_state;
    client::InventoryUiState inventory_ui_state;
    client::UiWindowsState ui_windows_state;
    std::unordered_map<std::string, client::SpriteSheetCacheEntry> monster_sheet_cache;
    std::unordered_map<std::string, client::SpriteSheetCacheEntry> item_sheet_cache;
    std::unordered_map<std::string, client::ItemUiDef> item_defs_by_key;
    std::unordered_map<std::string, MonsterDef> monster_defs_by_id;
    float move_repeat_timer = 0.0f;
    bool move_repeat_started = false;
    float rotate_repeat_timer = 0.0f;
    bool rotate_repeat_started = false;
    bool chat_input_active = false;
    client::AuthUiState auth_ui;
    client::initAuthUi(auth_ui);

    client::SceneConfig scene_cfg{};
    const client::InventoryUiConfig inventory_cfg{};
    const client::UiSettings ui_settings = client::loadUiSettings("data/global.toml");
    scene_cfg.speech_bubble_alpha = ui_settings.speech_bubble_alpha;
    scene_cfg.player_name_text_size = ui_settings.player_name_text_size;
    scene_cfg.monster_name_text_size = ui_settings.monster_name_text_size;
    scene_cfg.npc_name_text_size = ui_settings.npc_name_text_size;
    scene_cfg.speech_text_size = ui_settings.speech_text_size;

    for (const auto& def : client::loadClientItemDefs("game/items")) {
        item_defs_by_key[client::normalizeKey(def.id)] = def;
        item_defs_by_key[client::normalizeKey(def.name)] = def;
    }
    for (const auto& def : loadMonsterDefs("game/monsters")) {
        monster_defs_by_id[client::normalizeKey(def.id)] = def;
    }

    while (!WindowShouldClose()) {
        if (auto login = mailbox.pop<LoginResultMsg>(MsgType::LoginResult)) {
            client::onAuthLoginResult(auth_ui, *login, logs);
        }
        if (client::tickAndDrawAuthUi(auth_ui, mailbox, ui_font, logs)) {
            continue;
        }

        if (chat_input_active) {
            while (int key = GetCharPressed()) {
                if (key >= 32 && key <= 126 && input.size() < kMaxInputChars) {
                    input.push_back(static_cast<char>(key));
                }
            }
        }

        if (chat_input_active && IsKeyPressed(KEY_BACKSPACE) && !input.empty()) {
            input.pop_back();
        }

        if (IsKeyPressed(KEY_ENTER)) {
            if (!chat_input_active) {
                chat_input_active = true;
                input.clear();
            } else {
                if (!input.empty()) {
                    LoginMsg login;
                    PickupMsg pickup;
                    DropMsg drop;
                    MoveMsg move;
                    AttackMsg attack;

                    if (client::tryParseLogin(input, login)) {
                        client::pushBounded(logs, "Use the login screen to authenticate");
                    } else if (client::tryParsePickup(input, pickup)) {
                        mailbox.push(MsgType::Pickup, pickup);
                        client::pushBounded(logs, "Pickup requested");
                    } else if (client::tryParseDrop(input, drop)) {
                        mailbox.push(MsgType::Drop, drop);
                        client::pushBounded(logs, "Drop requested");
                    } else if (client::tryParseMove(input, move)) {
                        mailbox.push(MsgType::Move, move);
                    } else if (client::tryParseAttack(input, attack)) {
                        mailbox.push(MsgType::Attack, attack);
                    } else if (input == "/help") {
                        client::pushBounded(logs, "Commands: /login /move /pickup [id] /drop <idx> /attack [id] /talk /think /yell");
                    } else {
                        ChatMsg chat{};
                        if (parseSpeechInput(input, chat)) mailbox.push(MsgType::ChatSend, chat);
                        else client::pushBounded(logs, "Unknown command. Use /help");
                    }
                }
                input.clear();
                chat_input_active = false;
            }
        }

        if (game_state.has_value() && !chat_input_active) {
            int mx = 0;
            int my = 0;
            if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) my = -1;
            else if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) my = 1;
            if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) mx = -1;
            else if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) mx = 1;

            const bool moving = (mx != 0 || my != 0);
            const float dt = GetFrameTime();
            const bool rotating_only = moving && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
            if (!moving) {
                move_repeat_timer = 0.0f;
                move_repeat_started = false;
                rotate_repeat_timer = 0.0f;
                rotate_repeat_started = false;
            } else {
                if (rotating_only) {
                    move_repeat_timer = 0.0f;
                    move_repeat_started = false;
                    rotate_repeat_timer -= dt;
                    if (!rotate_repeat_started) {
                        mailbox.push(MsgType::Rotate, RotateMsg{mx, my});
                        rotate_repeat_started = true;
                        rotate_repeat_timer = 0.18f;
                    } else if (rotate_repeat_timer <= 0.0f) {
                        mailbox.push(MsgType::Rotate, RotateMsg{mx, my});
                        rotate_repeat_timer = 0.085f;
                    }
                } else {
                    rotate_repeat_timer = 0.0f;
                    rotate_repeat_started = false;
                    move_repeat_timer -= dt;
                    if (!move_repeat_started) {
                        mailbox.push(MsgType::Move, MoveMsg{mx, my});
                        move_repeat_started = true;
                        move_repeat_timer = 0.20f;
                    } else if (move_repeat_timer <= 0.0f) {
                        mailbox.push(MsgType::Move, MoveMsg{mx, my});
                        move_repeat_timer = 0.085f;
                    }
                }
            }

            if (IsKeyPressed(KEY_SPACE)) mailbox.push(MsgType::Attack, AttackMsg{game_state->attack_target_monster_id});
            if (IsKeyPressed(KEY_G)) mailbox.push(MsgType::Pickup, PickupMsg{-1});
            if (IsKeyPressed(KEY_B)) mailbox.push(MsgType::Drop, DropMsg{0});
        }

        if (auto chat = mailbox.pop<ChatMsg>(MsgType::Chat)) {
            const std::string from = chat->from.empty()
                ? (game_state ? game_state->your_user : std::string{})
                : chat->from;
            if (!from.empty() && !chat->text.empty()) {
                scene_state.speech_text_by_user[from] = chat->text;
                scene_state.speech_type_by_user[from] = chat->speech_type.empty() ? "talk" : chat->speech_type;
                scene_state.speech_timer_by_user[from] = 3.5f;
                scene_state.speech_seq_by_user[from] = ++scene_state.speech_seq_counter;
                client::pushBounded(logs, from + ": " + chat->text);
            }
        }

        if (auto room = mailbox.pop<Room>(MsgType::Room)) {
            current_room = std::move(*room);
            rr.load(*current_room);
            scene_state.prev_pos_by_key.clear();
            scene_state.render_pos_by_key.clear();
            scene_state.anim_by_key.clear();
            scene_state.last_attack_seq_by_key.clear();
            scene_state.last_combat_outcome_seq_by_key.clear();
            scene_state.attack_fx_timer_by_key.clear();
            scene_state.combat_outcome_fx_by_key.clear();
            scene_state.speech_text_by_user.clear();
            scene_state.speech_type_by_user.clear();
            scene_state.speech_timer_by_user.clear();
            scene_state.speech_seq_by_user.clear();
            scene_state.dragging_ground_item_id = -1;
        }

        if (auto state = mailbox.pop<GameStateMsg>(MsgType::GameState)) {
            const bool room_changed = !last_game_state_room.empty() && state->your_room != last_game_state_room;
            game_state = std::move(*state);
            if (room_changed) {
                scene_state.prev_pos_by_key.clear();
                scene_state.render_pos_by_key.clear();
                scene_state.anim_by_key.clear();
                scene_state.last_attack_seq_by_key.clear();
                scene_state.last_combat_outcome_seq_by_key.clear();
                scene_state.attack_fx_timer_by_key.clear();
                scene_state.combat_outcome_fx_by_key.clear();
                scene_state.speech_text_by_user.clear();
                scene_state.speech_type_by_user.clear();
                scene_state.speech_timer_by_user.clear();
                scene_state.speech_seq_by_user.clear();
                scene_state.dragging_ground_item_id = -1;
            }
            last_game_state_room = game_state->your_room;
            client::updateMonsterSheetCache(*game_state, monster_sheet_cache);
            client::updateNpcSheetCache(*game_state, monster_sheet_cache);
            client::updateItemSheetCacheFromGroundItems(*game_state, item_sheet_cache);
            client::updateItemSheetCacheFromInventory(*game_state, item_defs_by_key, monster_defs_by_id, item_sheet_cache);
            if (!game_state->event_text.empty() && game_state->event_text != last_event) {
                last_event = game_state->event_text;
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);

        const int side_w = static_cast<int>(inventory_cfg.panel_w);
        const int bottom_h = kGameplayBottomPanelPx;
        const client::PlayLayout play_layout = client::computePlayLayout(
            current_room, side_w, bottom_h, kPlayViewportMaxWidthRatio, kPlayViewportMaxHeightRatio, scene_cfg.map_scale);
        const Rectangle play_rect = play_layout.play_rect;
        const int play_x = static_cast<int>(play_rect.x);
        const int play_y = static_cast<int>(play_rect.y);
        const int play_w = static_cast<int>(play_rect.width);
        const int play_h = static_cast<int>(play_rect.height);

        // Right-lane windows are persisted in state; we only seed defaults once.
        const float right_x = static_cast<float>(GetScreenWidth()) - inventory_cfg.panel_w - kDockMarginRight;
        auto& vitals_window = client::ensureWindow(ui_windows_state, "vitals", "Health / Mana", Rectangle{right_x, kDockMarginTop, inventory_cfg.panel_w, 106.0f}, true);
        auto& inventory_window = client::ensureWindow(ui_windows_state, "inventory", "Inventory", Rectangle{right_x, 126.0f, inventory_cfg.panel_w, 420.0f}, true);
        auto& skills_window = client::ensureWindow(ui_windows_state, "skills", "Skills", Rectangle{right_x, 556.0f, inventory_cfg.panel_w, 320.0f}, true);
        constexpr int kInvSlotLimit = 8;
        const int inv_rows = std::max(1, (kInvSlotLimit + inventory_cfg.cols - 1) / inventory_cfg.cols);
        const float inv_body_h = 12.0f + inv_rows * inventory_cfg.slot_size + (inv_rows - 1) * inventory_cfg.gap + 12.0f;
        inventory_window.rect.height = client::kUiWindowHeaderHeight + inv_body_h;
        vitals_window.rect.width = inventory_cfg.panel_w;
        inventory_window.rect.width = inventory_cfg.panel_w;
        skills_window.rect.width = inventory_cfg.panel_w;
        vitals_window.rect.x = right_x;
        inventory_window.rect.x = right_x;
        skills_window.rect.x = right_x;

        if (ui_windows_state.dragging_id.empty()) {
            client::snapDockedWindows({&vitals_window, &inventory_window, &skills_window},
                                      right_x,
                                      kDockMarginTop,
                                      kDockWindowGapY);
        }
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{16, 18, 22, 255});
        DrawRectangleRec(play_rect, BLACK);
        DrawRectangleLinesEx(play_rect, 1.0f, Color{72, 76, 84, 255});

        const float render_scale = play_layout.render_scale;
        const float map_draw_w = play_layout.map_draw_w;
        const float map_draw_h = play_layout.map_draw_h;
        const float map_origin_x = play_layout.map_origin_x;
        const float map_origin_y = play_layout.map_origin_y;

        // World pass: background layers, entities, then foreground layers.
        // Scissor guarantees world drawing never bleeds outside play viewport.
        BeginScissorMode(play_x, play_y, play_w, play_h);
        if (current_room) {
            rr.drawUnderEntities(*current_room, render_scale, Vector2{map_origin_x, map_origin_y});
        }

        if (current_room && game_state) {
            client::SceneOutput scene_out{};
            client::SceneConfig draw_cfg = scene_cfg;
            draw_cfg.map_scale = render_scale;
            draw_cfg.map_origin_x = map_origin_x;
            draw_cfg.map_origin_y = map_origin_y;
            draw_cfg.map_view_width = map_draw_w;
            draw_cfg.map_view_height = map_draw_h;
            draw_cfg.inventory_panel_width = inventory_cfg.panel_w;
            draw_cfg.inventory_top_offset = inventory_window.rect.y;
            draw_cfg.inventory_drop_rect = inventory_window.collapsed
                ? Rectangle{0, 0, 0, 0}
                : inventory_window.rect;
            draw_cfg.bottom_panel_height = static_cast<float>(bottom_h);
            draw_cfg.inventory_visible = !inventory_window.collapsed;
            const auto monster_sheet_view = [&](const std::string& tsx) -> client::SpriteSheetView {
                auto it = monster_sheet_cache.find(tsx);
                if (it == monster_sheet_cache.end()) return {};
                return client::SpriteSheetView{
                    &it->second.sprites,
                    it->second.tex,
                    it->second.ready
                };
            };
            const auto item_sheet_view = [&](const std::string& tsx) -> client::ItemSheetView {
                auto it = item_sheet_cache.find(tsx);
                if (it == item_sheet_cache.end()) return {};
                return client::ItemSheetView{
                    &it->second.sprites,
                    it->second.tex,
                    it->second.ready
                };
            };
            client::drawScene(*current_room,
                              *game_state,
                              sprites,
                              character_tex,
                              sprite_ready,
                              combat_fx_tex,
                              combat_fx_ready,
                              speech_tex,
                              speech_ready,
                              monster_sheet_view,
                              item_sheet_view,
                              ui_font,
                              GetFrameTime(),
                              scene_state,
                              draw_cfg,
                              scene_out);
            if (scene_out.attack_click.has_value()) {
                mailbox.push(MsgType::Attack, *scene_out.attack_click);
            }
            if (scene_out.move_ground_item.has_value()) {
                mailbox.push(MsgType::MoveGroundItem, *scene_out.move_ground_item);
            }
            if (scene_out.pickup_ground_item.has_value()) {
                mailbox.push(MsgType::Pickup, *scene_out.pickup_ground_item);
            }

        }
        if (current_room) {
            rr.drawAboveEntities(*current_room, render_scale, Vector2{map_origin_x, map_origin_y});
        }
        EndScissorMode();

        if (current_room && game_state) {
            client::SceneConfig bubble_cfg = scene_cfg;
            bubble_cfg.map_scale = render_scale;
            bubble_cfg.map_origin_x = map_origin_x;
            bubble_cfg.map_origin_y = map_origin_y;
            bubble_cfg.map_view_width = map_draw_w;
            bubble_cfg.map_view_height = map_draw_h;
            client::drawSpeechOverlays(*current_room,
                                       *game_state,
                                       speech_tex,
                                       speech_ready,
                                       ui_font,
                                       scene_state,
                                       bubble_cfg);
        }

        // UI pass: window frames first (for hit testing + collapse state),
        // then each window body contents.
        if (game_state) {
            const auto vitals_frame = client::drawWindowFrame(ui_windows_state, ui_font, vitals_window);
            const auto inv_frame = client::drawWindowFrame(ui_windows_state, ui_font, inventory_window);
            const auto skills_frame = client::drawWindowFrame(ui_windows_state, ui_font, skills_window);

            if (vitals_frame.open) {
                const float pad = 10.0f;
                const float bar_w = std::max(80.0f, vitals_frame.body_rect.width - pad * 2.0f);
                const float bx = vitals_frame.body_rect.x + (vitals_frame.body_rect.width - bar_w) * 0.5f;
                const float by = vitals_frame.body_rect.y + 10.0f;
                client::drawLabeledBar(ui_font, "HP", bx, by, bar_w, 22.0f, game_state->your_hp, game_state->your_max_hp, 115, 38, 38, 255);
                client::drawLabeledBar(ui_font, "MP", bx, by + 30.0f, bar_w, 22.0f, game_state->your_mana, game_state->your_max_mana, 50, 80, 160, 255);
            }

            client::InventoryUiOutput inv_out{};
            const auto draw_inventory_icon = [&](const std::string& item_name, const Rectangle& icon_rect) -> bool {
                auto dit = item_defs_by_key.find(client::normalizeKey(item_name));
                std::string tsx;
                std::string sprite_name;
                ClipKind kind = ClipKind::Move;
                if (dit != item_defs_by_key.end()) {
                    const auto& def = dit->second;
                    tsx = def.sprite_tileset;
                    sprite_name = def.id;
                } else {
                    const std::string corpse_id = client::parseCorpseMonsterId(item_name);
                    if (corpse_id.empty()) return false;
                    auto mit = monster_defs_by_id.find(corpse_id);
                    if (mit == monster_defs_by_id.end()) return false;
                    tsx = mit->second.sprite_tileset;
                    sprite_name = mit->second.id;
                    kind = ClipKind::Death;
                }
                auto it = item_sheet_cache.find(tsx);
                if (it == item_sheet_cache.end()) return false;
                if (!it->second.ready) return false;
                const Frame* fr = it->second.sprites.frame(sprite_name, Dir::S, 0, kind);
                if (!fr && kind == ClipKind::Death) fr = it->second.sprites.frame(sprite_name, Dir::S, 0, ClipKind::Move);
                if (!fr) fr = it->second.sprites.frame(sprite_name, Dir::W, 0, ClipKind::Move);
                if (!fr) fr = it->second.sprites.frame(sprite_name, Dir::S, 0, ClipKind::Move);
                if (!fr) return false;
                const Rectangle src = fr->rect();
                const float scale = std::min(icon_rect.width / src.width, icon_rect.height / src.height);
                Rectangle dst{
                    icon_rect.x + (icon_rect.width - src.width * scale) * 0.5f,
                    icon_rect.y + (icon_rect.height - src.height * scale) * 0.5f,
                    src.width * scale,
                    src.height * scale
                };
                DrawTexturePro(it->second.tex, src, dst, Vector2{0, 0}, 0.0f, WHITE);
                return true;
            };
            const auto resolve_item_equip_type = [&](const std::string& item_name) -> std::string {
                auto dit = item_defs_by_key.find(client::normalizeKey(item_name));
                if (dit == item_defs_by_key.end()) return {};
                return dit->second.equip_type;
            };

            if (inv_frame.open) {
                client::drawInventoryUi(ui_font,
                                        *game_state,
                                        inventory_ui_state,
                                        inventory_cfg,
                                        inv_frame.body_rect,
                                        draw_inventory_icon,
                                        resolve_item_equip_type,
                                        inv_out);
            }

            if (skills_frame.open) {
                const float pad = 10.0f;
                const float bar_w = std::max(80.0f, skills_frame.body_rect.width - pad * 2.0f);
                const float bx = skills_frame.body_rect.x + (skills_frame.body_rect.width - bar_w) * 0.5f;
                float by = skills_frame.body_rect.y + 6.0f;
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
                                      int r,
                                      int g,
                                      int b) {
                    client::drawUiText(ui_font,
                                       std::to_string(std::max(1, level)),
                                       bx,
                                       by + 1.0f,
                                       label_size,
                                       Color{220, 225, 235, 255});
                    client::drawLabeledBar(ui_font,
                                           name,
                                           prog_x,
                                           by,
                                           prog_w,
                                           row_h,
                                           xp,
                                           std::max(1, xp_to_next),
                                           r,
                                           g,
                                           b,
                                           255);
                    by += row_gap;
                };
                draw_skill("EXP", game_state->your_level, game_state->your_exp, std::max(1, game_state->your_exp_to_next_level), 153, 121, 60);
                draw_skill("Hit", game_state->skill_melee_level, game_state->skill_melee_xp, game_state->skill_melee_xp_to_next, 128, 88, 42);
                draw_skill("Block", game_state->skill_shielding_level, game_state->skill_shielding_xp, game_state->skill_shielding_xp_to_next, 72, 104, 144);
                draw_skill("Evade", game_state->skill_evasion_level, game_state->skill_evasion_xp, game_state->skill_evasion_xp_to_next, 112, 112, 112);
                draw_skill("Distance", game_state->skill_distance_level, game_state->skill_distance_xp, game_state->skill_distance_xp_to_next, 80, 126, 80);
                draw_skill("Magic", game_state->skill_magic_level, game_state->skill_magic_xp, game_state->skill_magic_xp_to_next, 82, 72, 153);

                const float txt_size = 14.0f;
                const float y1 = std::max(by + 2.0f, skills_frame.body_rect.y + skills_frame.body_rect.height - 66.0f);
                client::drawUiText(ui_font,
                                   "Attack: " + std::to_string(game_state->trait_attack),
                                   bx,
                                   y1,
                                   txt_size,
                                   Color{255, 205, 164, 255});
                client::drawUiText(ui_font,
                                   "Shielding: " + std::to_string(game_state->trait_shielding),
                                   bx,
                                   y1 + 14.0f,
                                   txt_size,
                                   Color{190, 218, 255, 255});
                client::drawUiText(ui_font,
                                   "Evasion: " + std::to_string(game_state->trait_evasion),
                                   bx,
                                   y1 + 28.0f,
                                   txt_size,
                                   Color{225, 225, 225, 255});
                client::drawUiText(ui_font,
                                   "Armor: " + std::to_string(game_state->trait_armor),
                                   bx,
                                   y1 + 42.0f,
                                   txt_size,
                                   Color{203, 219, 177, 255});
            }

            if (inv_out.swap_msg.has_value()) {
                mailbox.push(MsgType::InventorySwap, *inv_out.swap_msg);
            }
            if (inv_out.drop_msg.has_value()) {
                DropMsg drop = *inv_out.drop_msg;
                if (current_room) {
                    // Convert drop target from screen-space mouse to map tile-space.
                    const Vector2 m = GetMousePosition();
                    if (m.x >= map_origin_x && m.y >= map_origin_y &&
                        m.x < (map_origin_x + map_draw_w) &&
                        m.y < (map_origin_y + map_draw_h)) {
                        const float tile_w = current_room->tile_width() * render_scale;
                        const float tile_h = current_room->tile_height() * render_scale;
                        if (tile_w > 0.0f && tile_h > 0.0f) {
                            drop.to_x = static_cast<int>(std::floor((m.x - map_origin_x) / tile_w));
                            drop.to_y = static_cast<int>(std::floor((m.y - map_origin_y) / tile_h));
                        }
                    }
                }
                mailbox.push(MsgType::Drop, drop);
            }
            if (inv_out.set_equipment_msg.has_value()) {
                mailbox.push(MsgType::SetEquipment, *inv_out.set_equipment_msg);
            }
        }

        client::drawChatInputOverlay(ui_font,
                                     play_x,
                                     play_y,
                                     play_w,
                                     play_h,
                                     kInputOverlayMargin,
                                     kInputOverlayHeight,
                                     kInputTextOffsetX,
                                     kInputTextOffsetY,
                                     kInputFontSize,
                                     input,
                                     chat_input_active);

        EndDrawing();
    }

    nc.stop();
    client::shutdownAuthUi(auth_ui);
    client::unloadSheetCache(monster_sheet_cache);
    client::unloadSheetCache(item_sheet_cache);
    if (owns_ui_font) UnloadFont(ui_font);
    if (combat_fx_tex.id != 0) UnloadTexture(combat_fx_tex);
    if (speech_tex.id != 0) UnloadTexture(speech_tex);
    if (character_tex.id != 0) UnloadTexture(character_tex);
    CloseWindow();
    return 0;
}
