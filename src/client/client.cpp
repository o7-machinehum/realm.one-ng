#include "raylib.h"

#include "client_support.h"
#include "client_ui_primitives.h"
#include "client_game_ui.h"
#include "client_hud.h"
#include "client_inventory_overlay.h"
#include "client_layout.h"
#include "client_scene_renderer.h"
#include "client_sheet_cache.h"
#include "string_util.h"
#include "ui_settings.h"
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
constexpr float kFallbackScale = 2.0f;
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
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(60);

    std::string host = "127.0.0.1";
    uint16_t port = 7000;
    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = static_cast<uint16_t>(std::stoi(argv[2]));

    Mailbox mailbox;
    RoomRenderer rr;
    NetClient nc(mailbox, host, port);
    nc.start();

    auto fatalAssetError = [](const char* msg) {
        while (!WindowShouldClose() && !GetKeyPressed()) {
            BeginDrawing();
            ClearBackground(BLACK);
            DrawText(msg, 20, 20, 20, RED);
            DrawText("Press any key to quit.", 20, 50, 16, LIGHTGRAY);
            EndDrawing();
        }
        CloseWindow();
    };

    Sprites sprites;
    Sprites::SizeOverrideMap player_size_overrides;
    player_size_overrides["player_1"] = {1, 2};
    if (!sprites.loadTSX("game/assets/art/character.tsx", player_size_overrides)) {
        fatalAssetError("FATAL: Failed to load game/assets/art/character.tsx");
        return 1;
    }
    const std::string character_tex_path = "game/assets/art/" + sprites.image_source();
    Texture2D character_tex = LoadTexture(character_tex_path.c_str());
    if (character_tex.id == 0) {
        fatalAssetError("FATAL: Failed to load character texture");
        return 1;
    }
    Font ui_font = client::loadUIFont();
    if (ui_font.texture.id == 0) {
        fatalAssetError("FATAL: Failed to load UI font");
        return 1;
    }
    Font ui_bold_font = client::loadUIBoldFont();
    if (ui_bold_font.texture.id == 0) {
        fatalAssetError("FATAL: Failed to load UI bold font");
        return 1;
    }
    Texture2D speech_tex = LoadTexture("game/assets/art/speech.png");
    if (speech_tex.id == 0) {
        fatalAssetError("FATAL: Failed to load game/assets/art/speech.png");
        return 1;
    }
    Texture2D combat_fx_tex = LoadTexture("game/assets/art/properties.png");
    if (combat_fx_tex.id == 0) {
        fatalAssetError("FATAL: Failed to load game/assets/art/properties.png");
        return 1;
    }
    Texture2D hotbar_tex = LoadTexture("game/assets/art/widgets/hotbar.png");
    if (hotbar_tex.id == 0) {
        fatalAssetError("FATAL: Failed to load game/assets/art/widgets/hotbar.png");
        return 1;
    }
    SetTextureFilter(hotbar_tex, TEXTURE_FILTER_POINT);
    Texture2D equip_tex = LoadTexture("game/assets/art/widgets/equiptment_bar.png");
    if (equip_tex.id == 0) {
        fatalAssetError("FATAL: Failed to load game/assets/art/widgets/equiptment_bar.png");
        return 1;
    }
    SetTextureFilter(equip_tex, TEXTURE_FILTER_POINT);

    std::optional<Room> current_room;
    std::optional<GameStateMsg> game_state;

    // Room transition pan state
    std::optional<Room> prev_room;
    RoomRenderer prev_rr;
    float transition_timer = 0.0f;
    constexpr float transition_duration = 0.35f;
    int transition_dir_x = 0;
    int transition_dir_y = 0;
    std::string last_game_state_room;
    std::deque<std::string> logs;
    std::string input;
    std::string last_event;
    client::SceneState scene_state;
    client::HudState hud_state;
    client::InventoryOverlayState overlay_state;
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

        const float dt = GetFrameTime();

        // Tick overlay fade
        client::tickInventoryOverlay(overlay_state, dt);

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

        // I key: toggle inventory overlay
        if (!chat_input_active && IsKeyPressed(KEY_I)) {
            overlay_state.visible = !overlay_state.visible;
        }

        // ESC: close overlay if open
        if (!chat_input_active && IsKeyPressed(KEY_ESCAPE) && overlay_state.visible) {
            overlay_state.visible = false;
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

        if (game_state.has_value() && !chat_input_active && !overlay_state.visible) {
            int mx = 0;
            int my = 0;
            if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) my = -1;
            else if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) my = 1;
            if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) mx = -1;
            else if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) mx = 1;

            const bool moving = (mx != 0 || my != 0);
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

        // --- Print chat to the screen ---
        if (auto chat = mailbox.pop<ChatMsg>(MsgType::Chat)) {
            const std::string from = chat->from;
            if (!from.empty() && !chat->text.empty()) {
                scene_state.speech_text_by_user[from] = chat->text;
                scene_state.speech_type_by_user[from] = chat->speech_type.empty() ? "talk" : chat->speech_type;
                scene_state.speech_timer_by_user[from] = 3.5f;
                scene_state.speech_seq_by_user[from] = ++scene_state.speech_seq_counter;
                client::pushBounded(logs, from + ": " + chat->text);
            }
        }

        if (auto room = mailbox.pop<Room>(MsgType::Room)) {
            // Detect pan direction from player's edge position in old room
            transition_dir_x = 0;
            transition_dir_y = 0;
            if (current_room && game_state) {
                const int px = game_state->your_x;
                const int py = game_state->your_y;
                const int mw = current_room->map_width();
                const int mh = current_room->map_height();
                if (px <= 0)      transition_dir_x = -1;
                if (px >= mw - 1) transition_dir_x =  1;
                if (py <= 0)      transition_dir_y = -1;
                if (py >= mh - 1) transition_dir_y =  1;
            }

            const bool do_pan = (transition_dir_x != 0 || transition_dir_y != 0);
            if (do_pan && current_room) {
                prev_room = std::move(current_room);
                prev_rr = std::move(rr);
                transition_timer = transition_duration;
            } else {
                prev_room.reset();
                prev_rr.unload();
                transition_timer = 0.0f;
            }

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

        // Tick room transition timer
        if (transition_timer > 0.0f) {
            transition_timer = std::max(0.0f, transition_timer - dt);
            if (transition_timer <= 0.0f) {
                prev_room.reset();
                prev_rr.unload();
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);

        const int screen_w = GetScreenWidth();
        const int screen_h = GetScreenHeight();

        // Fullscreen layout — map fills entire screen, HUD overlays on top
        const client::PlayLayout play_layout = client::computeFullscreenLayout(
            current_room, kFallbackScale);

        const float render_scale = play_layout.render_scale;
        const float map_origin_x = play_layout.map_origin_x;
        const float map_origin_y = play_layout.map_origin_y;
        const float map_draw_w = play_layout.map_draw_w;
        const float map_draw_h = play_layout.map_draw_h;

        // Compute transition offsets (zero when no transition active)
        const bool transitioning = (transition_timer > 0.0f && prev_room);
        float new_off_x = 0.0f, new_off_y = 0.0f;
        float old_off_x = 0.0f, old_off_y = 0.0f;
        if (transitioning) {
            const float progress = 1.0f - (transition_timer / transition_duration);
            old_off_x = -progress * static_cast<float>(screen_w) * transition_dir_x;
            old_off_y = -progress * static_cast<float>(screen_h) * transition_dir_y;
            new_off_x = (1.0f - progress) * static_cast<float>(screen_w) * transition_dir_x;
            new_off_y = (1.0f - progress) * static_cast<float>(screen_h) * transition_dir_y;
        }

        // World pass: full screen scissor
        BeginScissorMode(0, 0, screen_w, screen_h);

        // Draw old room sliding out
        if (transitioning) {
            prev_rr.drawUnderEntities(*prev_room, render_scale,
                Vector2{map_origin_x + old_off_x, map_origin_y + old_off_y});
            prev_rr.drawAboveEntities(*prev_room, render_scale,
                Vector2{map_origin_x + old_off_x, map_origin_y + old_off_y});
        }

        // Draw new room tiles
        if (current_room) {
            rr.drawUnderEntities(*current_room, render_scale,
                Vector2{map_origin_x + new_off_x, map_origin_y + new_off_y});
        }

        if (current_room && game_state) {
            client::SceneOutput scene_out{};
            client::SceneConfig draw_cfg = scene_cfg;
            draw_cfg.map_scale = render_scale;
            draw_cfg.map_origin_x = map_origin_x + new_off_x;
            draw_cfg.map_origin_y = map_origin_y + new_off_y;
            draw_cfg.map_view_width = map_draw_w;
            draw_cfg.map_view_height = map_draw_h;
            const auto monster_sheet_view = [&](const std::string& tsx) -> client::SpriteSheetView {
                auto it = monster_sheet_cache.find(tsx);
                if (it == monster_sheet_cache.end()) return {};
                return client::SpriteSheetView{&it->second.sprites, it->second.tex};
            };
            const auto item_sheet_view = [&](const std::string& tsx) -> client::ItemSheetView {
                auto it = item_sheet_cache.find(tsx);
                if (it == item_sheet_cache.end()) return {};
                return client::ItemSheetView{&it->second.sprites, it->second.tex};
            };
            client::drawScene(*current_room,
                              *game_state,
                              sprites,
                              character_tex,
                              combat_fx_tex,
                              speech_tex,
                              monster_sheet_view,
                              item_sheet_view,
                              ui_font,
                              dt,
                              scene_state,
                              draw_cfg,
                              scene_out);
            if (scene_out.attack_click.has_value()) {
                mailbox.push(MsgType::Attack, *scene_out.attack_click);
            }
            // If a ground item was "moved" but the mouse is over the HUD,
            // convert it to a pickup instead.
            if (scene_out.move_ground_item.has_value()) {
                const float hud_h = client::hudTotalHeight(static_cast<float>(screen_h));
                const float hud_top = static_cast<float>(screen_h) - hud_h - (kInputOverlayHeight + kInputOverlayMargin);
                if (GetMousePosition().y >= hud_top) {
                    mailbox.push(MsgType::Pickup, PickupMsg{scene_out.move_ground_item->item_id});
                } else {
                    mailbox.push(MsgType::MoveGroundItem, *scene_out.move_ground_item);
                }
            }
            if (scene_out.pickup_ground_item.has_value()) {
                mailbox.push(MsgType::Pickup, *scene_out.pickup_ground_item);
            }
        }
        if (current_room) {
            rr.drawAboveEntities(*current_room, render_scale,
                Vector2{map_origin_x + new_off_x, map_origin_y + new_off_y});
        }
        EndScissorMode();

        // Speech bubbles (drawn outside scissor so they can overlap HUD)
        if (current_room && game_state) {
            client::SceneConfig bubble_cfg = scene_cfg;
            bubble_cfg.map_scale = render_scale;
            bubble_cfg.map_origin_x = map_origin_x + new_off_x;
            bubble_cfg.map_origin_y = map_origin_y + new_off_y;
            bubble_cfg.map_view_width = map_draw_w;
            bubble_cfg.map_view_height = map_draw_h;
            client::drawSpeechOverlays(*current_room,
                                       *game_state,
                                       speech_tex,
                                       ui_font,
                                       ui_bold_font,
                                       scene_state,
                                       bubble_cfg);
        }

        // Draw item icon lambda (shared by HUD and overlay)
        const auto draw_inventory_icon = [&](const std::string& item_name, const Rectangle& icon_rect, Color tint) -> bool {
            auto dit = item_defs_by_key.find(client::normalizeKey(item_name));
            std::string tsx;
            std::string sprite_name;
            ClipKind kind = ClipKind::Move;
            if (dit != item_defs_by_key.end()) {
                const auto& def = dit->second;
                tsx = def.sprite_tileset;
                sprite_name = def.id;
            } else {
                const std::string corpse_id = parseCorpseMonsterId(item_name);
                if (corpse_id.empty()) return false;
                auto mit = monster_defs_by_id.find(corpse_id);
                if (mit == monster_defs_by_id.end()) return false;
                tsx = mit->second.sprite_tileset;
                sprite_name = mit->second.id;
                kind = ClipKind::Death;
            }
            auto it = item_sheet_cache.find(tsx);
            if (it == item_sheet_cache.end()) return false;
            if (it->second.tex.id == 0) return false;
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
            DrawTexturePro(it->second.tex, src, dst, Vector2{0, 0}, 0.0f, tint);
            return true;
        };
        const auto resolve_item_equip_type = [&](const std::string& item_name) -> std::optional<ItemType> {
            auto dit = item_defs_by_key.find(client::normalizeKey(item_name));
            if (dit == item_defs_by_key.end()) return std::nullopt;
            return dit->second.item_type;
        };

        const float chat_area_h = kInputOverlayHeight + kInputOverlayMargin;

        // === Inventory Overlay (drawn FIRST so HUD renders on top) ===
        if (game_state && overlay_state.alpha > 0.0f) {
            client::InventoryOverlayOutput inv_out{};
            client::drawInventoryOverlay(ui_font,
                                         *game_state,
                                         overlay_state,
                                         hud_state.drag,
                                         draw_inventory_icon,
                                         resolve_item_equip_type,
                                         inv_out);

            if (inv_out.set_equipment_msg.has_value()) {
                mailbox.push(MsgType::SetEquipment, *inv_out.set_equipment_msg);
            }
            if (inv_out.swap_msg.has_value()) {
                mailbox.push(MsgType::InventorySwap, *inv_out.swap_msg);
            }
        }

        // === Bottom HUD (drawn AFTER overlay, on top of dim background) ===
        if (game_state) {
            client::HudOutput hud_out{};
            client::drawHud(ui_font,
                            *game_state,
                            hud_state,
                            hotbar_tex,
                            equip_tex,
                            scene_state.dragging_ground_item_id,
                            overlay_state.visible,
                            chat_area_h,
                            dt,
                            draw_inventory_icon,
                            resolve_item_equip_type,
                            hud_out);

            if (hud_out.set_equipment_msg.has_value()) {
                mailbox.push(MsgType::SetEquipment, *hud_out.set_equipment_msg);
            }
            if (hud_out.pickup_ground_item.has_value()) {
                mailbox.push(MsgType::Pickup, *hud_out.pickup_ground_item);
            }
            if (hud_out.drop_msg.has_value()) {
                DropMsg drop = *hud_out.drop_msg;
                if (current_room) {
                    const Vector2 m = GetMousePosition();
                    const float eff_ox = map_origin_x + new_off_x;
                    const float eff_oy = map_origin_y + new_off_y;
                    const float tile_w = current_room->tile_width() * render_scale;
                    const float tile_h = current_room->tile_height() * render_scale;
                    if (tile_w > 0.0f && tile_h > 0.0f) {
                        drop.to_x = static_cast<int>(std::floor((m.x - eff_ox) / tile_w));
                        drop.to_y = static_cast<int>(std::floor((m.y - eff_oy) / tile_h));
                    }
                }
                mailbox.push(MsgType::Drop, drop);
            }
            if (hud_out.swap_msg.has_value()) {
                mailbox.push(MsgType::InventorySwap, *hud_out.swap_msg);
            }
        }

        // Chat input overlay at the very bottom of the screen
        // Chat matches HUD idle opacity, becomes visible on Enter or when overlay is open
        const float chat_alpha = (chat_input_active || overlay_state.visible)
                                 ? client::kHudActiveAlpha
                                 : client::kHudIdleAlpha;
        client::drawChatInputOverlay(ui_font,
                                     0,
                                     0,
                                     screen_w,
                                     screen_h,
                                     kInputOverlayMargin,
                                     kInputOverlayHeight,
                                     kInputTextOffsetX,
                                     kInputTextOffsetY,
                                     kInputFontSize,
                                     input,
                                     chat_input_active,
                                     chat_alpha);

        EndDrawing();
    }

    nc.stop();
    client::shutdownAuthUi(auth_ui);
    client::unloadSheetCache(monster_sheet_cache);
    client::unloadSheetCache(item_sheet_cache);
    UnloadFont(ui_bold_font);
    UnloadFont(ui_font);
    UnloadTexture(equip_tex);
    UnloadTexture(hotbar_tex);
    UnloadTexture(combat_fx_tex);
    UnloadTexture(speech_tex);
    UnloadTexture(character_tex);
    CloseWindow();
    return 0;
}
