#include "raylib.h"

#include "client_support.h"
#include "client_inventory_ui.h"
#include "client_scene_renderer.h"
#include "msg.h"
#include "net_client.h"
#include "room.h"
#include "room_render.h"
#include "sprites.h"

#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
struct SpriteSheetCacheEntry {
    Sprites sprites;
    Texture2D tex{};
    bool ready = false;
    Sprites::SizeOverrideMap size_overrides;
};

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string trim(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::string parseTomlString(const std::string& raw) {
    std::string v = trim(raw);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
        return v.substr(1, v.size() - 2);
    }
    return v;
}

struct ItemUiDef {
    std::string id;
    std::string name;
    std::string sprite_tileset;
    std::string sprite_name;
    std::string equip_type;
};

std::vector<ItemUiDef> loadClientItemDefs(const std::string& dir_path) {
    std::vector<ItemUiDef> out;
    namespace fs = std::filesystem;
    fs::path dir(dir_path);
    if (!fs::exists(dir) || !fs::is_directory(dir)) return out;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".toml") continue;
        ItemUiDef def;
        def.id = entry.path().stem().string();
        def.name = def.id;
        std::ifstream in(entry.path());
        if (!in) continue;
        std::string line;
        while (std::getline(in, line)) {
            const auto hash = line.find('#');
            if (hash != std::string::npos) line = line.substr(0, hash);
            line = trim(line);
            if (line.empty() || line.front() == '[') continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = trim(line.substr(0, eq));
            const std::string value = trim(line.substr(eq + 1));
            if (key == "name") def.name = parseTomlString(value);
            else if (key == "sprite_tileset") def.sprite_tileset = parseTomlString(value);
            else if (key == "sprite_name") def.sprite_name = parseTomlString(value);
            else if (key == "equip_type" || key == "item_type" || key == "type" || key == "slot") def.equip_type = parseTomlString(value);
        }
        if (def.sprite_tileset.empty()) def.sprite_tileset = "materials2.tsx";
        if (def.sprite_name.empty()) def.sprite_name = def.id;
        out.push_back(std::move(def));
    }
    return out;
}
} // namespace

int main(int argc, char** argv) {
    InitWindow(1200, 760, "The Island");
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
    bool sprite_ready = false;
    Sprites::SizeOverrideMap player_size_overrides;
    player_size_overrides["player_1"] = {1, 2};
    if (sprites.loadTSX("game/assets/art/character.tsx", player_size_overrides)) {
        const std::string tex_path = "game/assets/art/" + sprites.image_source();
        character_tex = LoadTexture(tex_path.c_str());
        sprite_ready = character_tex.id != 0;
    }
    bool owns_ui_font = false;
    Font ui_font = client::loadUIFont(owns_ui_font);

    std::optional<Room> current_room;
    std::optional<GameStateMsg> game_state;
    std::string last_game_state_room;
    std::deque<std::string> logs;
    std::string input;
    std::string last_event;
    client::SceneState scene_state;
    client::InventoryUiState inventory_ui_state;
    std::unordered_map<std::string, SpriteSheetCacheEntry> monster_sheet_cache;
    std::unordered_map<std::string, SpriteSheetCacheEntry> item_sheet_cache;
    std::unordered_map<std::string, ItemUiDef> item_defs_by_key;
    float move_repeat_timer = 0.0f;
    bool move_repeat_started = false;

    const client::SceneConfig scene_cfg{};
    const client::InventoryUiConfig inventory_cfg{};

    for (const auto& def : loadClientItemDefs("game/items")) {
        item_defs_by_key[toLower(def.id)] = def;
        item_defs_by_key[toLower(def.name)] = def;
    }

    client::pushBounded(logs, "Type /login <user> <pass>.");
    client::pushBounded(logs, "Controls: arrows/WASD move, right-click monster to attack, G pickup, B drop slot 0.");

    while (!WindowShouldClose()) {
        while (int key = GetCharPressed()) {
            if (key >= 32 && key <= 126 && input.size() < 180) {
                input.push_back(static_cast<char>(key));
            }
        }

        if (IsKeyPressed(KEY_BACKSPACE) && !input.empty()) {
            input.pop_back();
        }

        if (IsKeyPressed(KEY_ENTER)) {
            if (!input.empty()) {
                LoginMsg login;
                PickupMsg pickup;
                DropMsg drop;
                MoveMsg move;
                AttackMsg attack;

                if (client::tryParseLogin(input, login)) {
                    mailbox.push(MsgType::Login, login);
                    client::pushBounded(logs, "Sent /login for " + login.user);
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
                    client::pushBounded(logs, "Commands: /login /move /pickup [id] /drop <idx> /attack [monster_id]");
                } else {
                    client::pushBounded(logs, "Unknown command. Use /help");
                }
            }
            input.clear();
        }

        if (game_state.has_value()) {
            int mx = 0;
            int my = 0;
            if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) my = -1;
            else if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) my = 1;
            if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) mx = -1;
            else if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) mx = 1;

            const bool moving = (mx != 0 || my != 0);
            const float dt = GetFrameTime();
            if (!moving) {
                move_repeat_timer = 0.0f;
                move_repeat_started = false;
            } else {
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

            if (IsKeyPressed(KEY_SPACE)) mailbox.push(MsgType::Attack, AttackMsg{game_state->attack_target_monster_id});
            if (IsKeyPressed(KEY_G)) mailbox.push(MsgType::Pickup, PickupMsg{-1});
            if (IsKeyPressed(KEY_B)) mailbox.push(MsgType::Drop, DropMsg{0});
        }

        if (auto login = mailbox.pop<LoginResultMsg>(MsgType::LoginResult)) {
            if (login->ok) {
                client::pushBounded(logs, "Login success: " + login->user + " room=" + login->room);
            } else {
                client::pushBounded(logs, "Login failed: " + login->message);
            }
        }

        if (auto room = mailbox.pop<Room>(MsgType::Room)) {
            current_room = std::move(*room);
            rr.load(*current_room);
            scene_state.prev_pos_by_key.clear();
            scene_state.render_pos_by_key.clear();
            scene_state.anim_by_key.clear();
            scene_state.dragging_ground_item_id = -1;
        }

        if (auto state = mailbox.pop<GameStateMsg>(MsgType::GameState)) {
            const bool room_changed = !last_game_state_room.empty() && state->your_room != last_game_state_room;
            game_state = std::move(*state);
            if (room_changed) {
                scene_state.prev_pos_by_key.clear();
                scene_state.render_pos_by_key.clear();
                scene_state.anim_by_key.clear();
                scene_state.dragging_ground_item_id = -1;
            }
            last_game_state_room = game_state->your_room;
            for (const auto& m : game_state->monsters) {
                if (m.sprite_tileset.empty()) continue;
                auto& entry = monster_sheet_cache[m.sprite_tileset];
                const std::string size_key = m.sprite_name.empty() ? toLower(m.name) : toLower(m.sprite_name);
                const std::pair<int, int> size_val{
                    std::max(1, m.sprite_w_tiles),
                    std::max(1, m.sprite_h_tiles)
                };
                bool needs_reload = !entry.ready;
                auto sit = entry.size_overrides.find(size_key);
                if (sit == entry.size_overrides.end() || sit->second != size_val) {
                    entry.size_overrides[size_key] = size_val;
                    needs_reload = true;
                }
                if (!needs_reload) continue;

                if (entry.tex.id != 0) {
                    UnloadTexture(entry.tex);
                    entry.tex = Texture2D{};
                }
                const std::string tsx_path = "game/assets/art/" + m.sprite_tileset;
                if (entry.sprites.loadTSX(tsx_path, entry.size_overrides)) {
                    const std::string tex_path = "game/assets/art/" + entry.sprites.image_source();
                    entry.tex = LoadTexture(tex_path.c_str());
                    entry.ready = (entry.tex.id != 0);
                } else {
                    entry.ready = false;
                }
            }
            for (const auto& i : game_state->items) {
                if (i.sprite_tileset.empty() || i.sprite_name.empty()) continue;
                auto& entry = item_sheet_cache[i.sprite_tileset];
                const std::string size_key = toLower(i.sprite_name);
                const std::pair<int, int> size_val{1, 1};
                bool needs_reload = !entry.ready;
                auto sit = entry.size_overrides.find(size_key);
                if (sit == entry.size_overrides.end() || sit->second != size_val) {
                    entry.size_overrides[size_key] = size_val;
                    needs_reload = true;
                }
                if (!needs_reload) continue;

                if (entry.tex.id != 0) {
                    UnloadTexture(entry.tex);
                    entry.tex = Texture2D{};
                }
                const std::string tsx_path = "game/assets/art/" + i.sprite_tileset;
                if (entry.sprites.loadTSX(tsx_path, entry.size_overrides)) {
                    const std::string tex_path = "game/assets/art/" + entry.sprites.image_source();
                    entry.tex = LoadTexture(tex_path.c_str());
                    entry.ready = (entry.tex.id != 0);
                } else {
                    entry.ready = false;
                }
            }
            for (const auto& inv_item : game_state->inventory) {
                auto dit = item_defs_by_key.find(toLower(inv_item));
                if (dit == item_defs_by_key.end()) continue;
                const auto& def = dit->second;
                auto& entry = item_sheet_cache[def.sprite_tileset];
                const std::string size_key = toLower(def.sprite_name);
                const std::pair<int, int> size_val{1, 1};
                bool needs_reload = !entry.ready;
                auto sit = entry.size_overrides.find(size_key);
                if (sit == entry.size_overrides.end() || sit->second != size_val) {
                    entry.size_overrides[size_key] = size_val;
                    needs_reload = true;
                }
                if (!needs_reload) continue;

                if (entry.tex.id != 0) {
                    UnloadTexture(entry.tex);
                    entry.tex = Texture2D{};
                }
                const std::string tsx_path = "game/assets/art/" + def.sprite_tileset;
                if (entry.sprites.loadTSX(tsx_path, entry.size_overrides)) {
                    const std::string tex_path = "game/assets/art/" + entry.sprites.image_source();
                    entry.tex = LoadTexture(tex_path.c_str());
                    entry.ready = (entry.tex.id != 0);
                } else {
                    entry.ready = false;
                }
            }
            if (!game_state->event_text.empty() && game_state->event_text != last_event) {
                last_event = game_state->event_text;
                client::pushBounded(logs, game_state->event_text);
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);

        const int side_w = static_cast<int>(inventory_cfg.panel_w);
        const int bottom_h = inventory_cfg.bottom_panel_height;
        const int play_w = std::max(1, GetScreenWidth() - side_w);
        const int play_h = std::max(1, GetScreenHeight() - bottom_h);
        const Rectangle play_rect{0.0f, 0.0f, static_cast<float>(play_w), static_cast<float>(play_h)};
        DrawRectangleRec(play_rect, BLACK);
        float render_scale = scene_cfg.map_scale;
        if (current_room) {
            const float map_px_w = static_cast<float>(current_room->map_width() * current_room->tile_width());
            const float map_px_h = static_cast<float>(current_room->map_height() * current_room->tile_height());
            if (map_px_w > 0.0f && map_px_h > 0.0f) {
                const float fill_width_scale = static_cast<float>(play_w) / map_px_w;
                render_scale = std::max(0.5f, fill_width_scale);
            }
        }

        BeginScissorMode(0, 0, play_w, play_h);
        if (current_room) {
            rr.drawUnderEntities(*current_room, render_scale);
        }

        if (current_room && game_state) {
            client::SceneOutput scene_out{};
            client::SceneConfig draw_cfg = scene_cfg;
            draw_cfg.map_scale = render_scale;
            draw_cfg.inventory_panel_width = inventory_cfg.panel_w;
            draw_cfg.bottom_panel_height = static_cast<float>(bottom_h);
            draw_cfg.inventory_visible = true;
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
            rr.drawAboveEntities(*current_room, render_scale);
        }
        EndScissorMode();

        if (game_state) {
            client::InventoryUiOutput inv_out{};
            const auto draw_inventory_icon = [&](const std::string& item_name, const Rectangle& icon_rect) -> bool {
                auto dit = item_defs_by_key.find(toLower(item_name));
                if (dit == item_defs_by_key.end()) return false;
                const auto& def = dit->second;
                auto it = item_sheet_cache.find(def.sprite_tileset);
                if (it == item_sheet_cache.end()) return false;
                if (!it->second.ready) return false;
                const Frame* fr = it->second.sprites.frame(def.sprite_name, Dir::W, 0);
                if (!fr) fr = it->second.sprites.frame(def.sprite_name, Dir::S, 0);
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
                auto dit = item_defs_by_key.find(toLower(item_name));
                if (dit == item_defs_by_key.end()) return {};
                return dit->second.equip_type;
            };
            client::drawInventoryUi(ui_font,
                                    *game_state,
                                    inventory_ui_state,
                                    inventory_cfg,
                                    draw_inventory_icon,
                                    resolve_item_equip_type,
                                    inv_out);
            if (inv_out.swap_msg.has_value()) {
                mailbox.push(MsgType::InventorySwap, *inv_out.swap_msg);
            }
            if (inv_out.drop_msg.has_value()) {
                DropMsg drop = *inv_out.drop_msg;
                if (current_room) {
                    const Vector2 m = GetMousePosition();
                    if (m.x >= 0.0f && m.y >= 0.0f &&
                        m.x < static_cast<float>(play_w) &&
                        m.y < static_cast<float>(play_h)) {
                        const float tile_w = current_room->tile_width() * render_scale;
                        const float tile_h = current_room->tile_height() * render_scale;
                        if (tile_w > 0.0f && tile_h > 0.0f) {
                            drop.to_x = static_cast<int>(std::floor(m.x / tile_w));
                            drop.to_y = static_cast<int>(std::floor(m.y / tile_h));
                        }
                    }
                }
                mailbox.push(MsgType::Drop, drop);
            }
            if (inv_out.set_equipment_msg.has_value()) {
                mailbox.push(MsgType::SetEquipment, *inv_out.set_equipment_msg);
            }
        }

        DrawRectangle(0, GetScreenHeight() - inventory_cfg.bottom_panel_height, GetScreenWidth(), inventory_cfg.bottom_panel_height, Fade(BLACK, 0.88f));
        DrawRectangleLines(0, GetScreenHeight() - inventory_cfg.bottom_panel_height, GetScreenWidth(), inventory_cfg.bottom_panel_height, GRAY);

        int y = GetScreenHeight() - inventory_cfg.bottom_panel_height + 24;
        for (const auto& line : logs) {
            client::drawUiText(ui_font, line, 10, static_cast<float>(y), 15, RAYWHITE);
            y += 19;
        }

        const std::string prompt = "> " + input;
        client::drawUiText(ui_font, prompt, 10, static_cast<float>(GetScreenHeight() - 32), 17, YELLOW);

        EndDrawing();
    }

    nc.stop();
    for (auto& [_, entry] : monster_sheet_cache) {
        if (entry.tex.id != 0) UnloadTexture(entry.tex);
    }
    for (auto& [_, entry] : item_sheet_cache) {
        if (entry.tex.id != 0) UnloadTexture(entry.tex);
    }
    if (owns_ui_font) UnloadFont(ui_font);
    if (character_tex.id != 0) UnloadTexture(character_tex);
    CloseWindow();
    return 0;
}
