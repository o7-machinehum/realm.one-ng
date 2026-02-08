#include "client_scene_renderer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

namespace client {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string pickSpriteName(const Sprites& sprites, const MonsterStateMsg& m) {
    const std::string requested = m.sprite_name;
    const std::string req_lower = toLower(requested);
    const std::string name_lower = toLower(m.name);

    const auto has_any = [&](const std::string& n) {
        return sprites.frame_count(n, Dir::S) > 0 ||
               sprites.frame_count(n, Dir::E) > 0 ||
               sprites.frame_count(n, Dir::N) > 0 ||
               sprites.frame_count(n, Dir::W) > 0;
    };

    if (!requested.empty() && has_any(requested)) return requested;
    if (!req_lower.empty() && has_any(req_lower)) return req_lower;
    if (!name_lower.empty() && has_any(name_lower)) return name_lower;
    if (has_any("player_1")) return "player_1";
    return requested.empty() ? "player_1" : requested;
}

std::pair<float, float> smoothPos(std::unordered_map<std::string, std::pair<float, float>>& render_pos_by_key,
                                  const std::string& key,
                                  int target_x,
                                  int target_y,
                                  float dt,
                                  float speed_tiles_per_sec) {
    auto [it, inserted] = render_pos_by_key.try_emplace(key, std::pair<float, float>{
        static_cast<float>(target_x),
        static_cast<float>(target_y)
    });

    float cx = it->second.first;
    float cy = it->second.second;
    const float tx = static_cast<float>(target_x);
    const float ty = static_cast<float>(target_y);
    const float dx = tx - cx;
    const float dy = ty - cy;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (inserted || dist < 0.0001f) return {cx, cy};

    const float max_step = std::max(0.0f, speed_tiles_per_sec) * std::max(0.0f, dt);
    if (max_step <= 0.0f || dist <= max_step) {
        cx = tx;
        cy = ty;
    } else {
        cx += dx / dist * max_step;
        cy += dy / dist * max_step;
    }
    it->second = {cx, cy};
    return it->second;
}

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
               SceneOutput& out) {
    out.attack_click.reset();
    out.move_ground_item.reset();
    scene_state.attack_fx_t += dt;

    const float tile_w = room.tile_width() * cfg.map_scale;
    const float tile_h = room.tile_height() * cfg.map_scale;
    std::vector<std::pair<int, Rectangle>> monster_click_boxes;
    struct MonsterVisual {
        const MonsterStateMsg* msg = nullptr;
        const Sprites* sprites = nullptr;
        Texture2D tex{};
        bool ready = false;
        AnimationComponent* anim = nullptr;
        Rectangle click_box{};
    };
    struct PlayerVisual {
        const PlayerStateMsg* msg = nullptr;
        float rx = 0.0f;
        float ry = 0.0f;
        AnimationComponent* anim = nullptr;
    };
    struct DrawCmd {
        float feet_y = 0.0f;
        int kind = 0; // 0 monster, 1 player
        size_t idx = 0;
    };
    std::vector<MonsterVisual> monster_visuals;
    std::vector<PlayerVisual> player_visuals;
    std::vector<DrawCmd> draw_cmds;

    struct GroundItemVisual {
        const GroundItemStateMsg* msg = nullptr;
        const Sprites* sprites = nullptr;
        Texture2D tex{};
        bool ready = false;
        Rectangle tile_box{};
    };
    std::vector<GroundItemVisual> ground_items;
    ground_items.reserve(game_state.items.size());
    for (const auto& item : game_state.items) {
        ItemSheetView sheet_view{};
        if (item_sheet_view) sheet_view = item_sheet_view(item.sprite_tileset);
        const Sprites* item_sprites = (sheet_view.ready && sheet_view.sprites) ? sheet_view.sprites : nullptr;
        const Texture2D item_tex = (sheet_view.ready && sheet_view.texture.id != 0) ? sheet_view.texture : Texture2D{};
        ground_items.push_back(GroundItemVisual{
            &item,
            item_sprites,
            item_tex,
            (item_sprites != nullptr && item_tex.id != 0),
            Rectangle{item.x * tile_w, item.y * tile_h, tile_w, tile_h}
        });
    }

    for (const auto& m : game_state.monsters) {
        SpriteSheetView sheet_view{};
        if (monster_sheet_view) sheet_view = monster_sheet_view(m.sprite_tileset);
        const Sprites* mon_sprites = (sheet_view.ready && sheet_view.sprites) ? sheet_view.sprites : &sprites;
        const Texture2D mon_tex = (sheet_view.ready && sheet_view.texture.id != 0) ? sheet_view.texture : character_tex;
        const bool mon_ready = (sheet_view.ready && sheet_view.sprites) ? true : sprite_ready;

        const std::string key = "m:" + std::to_string(m.id);
        auto& anim = scene_state.anim_by_key[key];
        anim.sprite_name = pickSpriteName(*mon_sprites, m);

        auto& prev = scene_state.prev_pos_by_key[key];
        const int dx = m.x - prev.first;
        const int dy = m.y - prev.second;
        anim.dir = dirFromDelta(dx, dy, anim.dir);
        tickAnimation(anim, *mon_sprites, true, dt);
        prev = {m.x, m.y};

        const float mw = std::max(1, m.sprite_w_tiles) * tile_w;
        const float mh = std::max(1, m.sprite_h_tiles) * tile_h;
        const float cell_x = m.x * tile_w;
        const float cell_y = (m.y - (std::max(1, m.sprite_h_tiles) - 1)) * tile_h;
        Rectangle click_box{cell_x, cell_y, mw, mh};

        monster_click_boxes.push_back({m.id, click_box});
        monster_visuals.push_back(MonsterVisual{
            &m, mon_sprites, mon_tex, mon_ready, &anim, click_box
        });
        draw_cmds.push_back(DrawCmd{m.y * tile_h, 0, monster_visuals.size() - 1});
    }

    for (const auto& p : game_state.players) {
        const std::string key = "p:" + p.user;
        auto& anim = scene_state.anim_by_key[key];
        anim.sprite_name = "player_1";

        auto& prev = scene_state.prev_pos_by_key[key];
        const int dx = p.x - prev.first;
        const int dy = p.y - prev.second;
        anim.dir = dirFromDelta(dx, dy, anim.dir);
        tickAnimation(anim, sprites, (dx != 0 || dy != 0), dt);
        prev = {p.x, p.y};
        const auto [rx, ry] = smoothPos(scene_state.render_pos_by_key,
                                        key,
                                        p.x,
                                        p.y,
                                        dt,
                                        cfg.player_slide_tiles_per_sec);

        player_visuals.push_back(PlayerVisual{&p, rx, ry, &anim});
        draw_cmds.push_back(DrawCmd{ry * tile_h, 1, player_visuals.size() - 1});
    }

    // Ground-space attack marker (behind sprites).
    for (const auto& mv : monster_visuals) {
        const auto& m = *mv.msg;
        if (game_state.attack_target_monster_id != m.id) continue;
        const float mw = std::max(1, m.sprite_w_tiles) * tile_w;
        Rectangle feet_box{m.x * tile_w, m.y * tile_h, mw, tile_h};
        DrawRectangleRec(feet_box, Fade(RED, 0.22f));
        DrawRectangleLinesEx(feet_box, 2.0f, RED);
    }

    auto findTopItemAt = [&](Vector2 mouse) -> int {
        for (auto it = ground_items.rbegin(); it != ground_items.rend(); ++it) {
            if (CheckCollisionPointRec(mouse, it->tile_box)) return it->msg->id;
        }
        return -1;
    };

    const Vector2 mouse = GetMousePosition();
    const bool mouse_in_map = mouse.x >= 0.0f &&
                              mouse.y >= 0.0f &&
                              mouse.x < (GetScreenWidth() - cfg.inventory_panel_width) &&
                              mouse.y < (GetScreenHeight() - cfg.bottom_panel_height);
    const int hovered_item_id = mouse_in_map ? findTopItemAt(mouse) : -1;

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && scene_state.dragging_ground_item_id < 0 && hovered_item_id >= 0) {
        scene_state.dragging_ground_item_id = hovered_item_id;
    }
    if (scene_state.dragging_ground_item_id >= 0 && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        if (mouse_in_map) {
            const int tx = static_cast<int>(std::floor(mouse.x / tile_w));
            const int ty = static_cast<int>(std::floor(mouse.y / tile_h));
            out.move_ground_item = MoveGroundItemMsg{scene_state.dragging_ground_item_id, tx, ty};
        }
        scene_state.dragging_ground_item_id = -1;
    }
    if (scene_state.dragging_ground_item_id >= 0 || hovered_item_id >= 0) {
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    } else {
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    }

    for (const auto& itv : ground_items) {
        if (itv.msg->id == scene_state.dragging_ground_item_id) continue;
        if (itv.ready && itv.sprites) {
            const Frame* fr = itv.sprites->frame(itv.msg->sprite_name, Dir::W, 0);
            if (!fr) fr = itv.sprites->frame(itv.msg->sprite_name, Dir::S, 0);
            if (fr) {
                const Rectangle src = fr->rect();
                Rectangle dst{itv.tile_box.x, itv.tile_box.y, src.width * cfg.map_scale, src.height * cfg.map_scale};
                dst.x += (tile_w - dst.width) * 0.5f;
                dst.y += tile_h - dst.height;
                DrawTexturePro(itv.tex, src, dst, Vector2{0, 0}, 0.0f, WHITE);
            } else {
                DrawRectangleRec(itv.tile_box, GOLD);
            }
        } else {
            DrawRectangle(static_cast<int>(itv.tile_box.x + tile_w * 0.25f),
                          static_cast<int>(itv.tile_box.y + tile_h * 0.25f),
                          static_cast<int>(tile_w * 0.5f),
                          static_cast<int>(tile_h * 0.5f),
                          GOLD);
        }
    }

    std::sort(draw_cmds.begin(), draw_cmds.end(), [](const DrawCmd& a, const DrawCmd& b) {
        if (a.feet_y != b.feet_y) return a.feet_y < b.feet_y;
        return a.kind < b.kind;
    });

    for (const auto& cmd : draw_cmds) {
        if (cmd.kind == 0) {
            const auto& mv = monster_visuals[cmd.idx];
            const auto& m = *mv.msg;
            if (mv.ready) {
                drawActor(*mv.sprites, mv.tex, *mv.anim, static_cast<float>(m.x), static_cast<float>(m.y), tile_w, tile_h, cfg.map_scale, PINK);
            } else {
                const float x = m.x * tile_w + tile_w * 0.5f;
                const float y = m.y * tile_h + tile_h * 0.5f;
                DrawCircle(static_cast<int>(x), static_cast<int>(y), 10, RED);
            }
        } else {
            const auto& pv = player_visuals[cmd.idx];
            const auto& p = *pv.msg;
            if (sprite_ready) {
                const Color tint = (p.user == game_state.your_user) ? WHITE : LIGHTGRAY;
                drawActor(sprites, character_tex, *pv.anim, pv.rx, pv.ry, tile_w, tile_h, cfg.map_scale, tint);
            } else {
                const float x = pv.rx * tile_w + tile_w * 0.5f;
                const float y = pv.ry * tile_h + tile_h * 0.5f;
                const Color c = (p.user == game_state.your_user) ? SKYBLUE : GREEN;
                DrawCircle(static_cast<int>(x), static_cast<int>(y), 9, c);
            }
        }
    }

    if (scene_state.dragging_ground_item_id >= 0) {
        const GroundItemVisual* drag_item = nullptr;
        for (const auto& itv : ground_items) {
            if (itv.msg->id == scene_state.dragging_ground_item_id) {
                drag_item = &itv;
                break;
            }
        }
        if (drag_item) {
            if (drag_item->ready && drag_item->sprites) {
                const Frame* fr = drag_item->sprites->frame(drag_item->msg->sprite_name, Dir::W, 0);
                if (!fr) fr = drag_item->sprites->frame(drag_item->msg->sprite_name, Dir::S, 0);
                if (fr) {
                    const Rectangle src = fr->rect();
                    Rectangle dst{mouse.x - (src.width * cfg.map_scale * 0.5f),
                                  mouse.y - (src.height * cfg.map_scale * 0.75f),
                                  src.width * cfg.map_scale,
                                  src.height * cfg.map_scale};
                    DrawTexturePro(drag_item->tex, src, dst, Vector2{0, 0}, 0.0f, Fade(WHITE, 0.85f));
                }
            } else {
                DrawCircleV(mouse, 8.0f, Fade(GOLD, 0.8f));
            }
        }
    }

    for (const auto& mv : monster_visuals) {
        const auto& m = *mv.msg;
        drawHealthBar(mv.click_box.x, mv.click_box.y - 8, mv.click_box.width, m.hp, m.max_hp);
        const float x = m.x * tile_w + tile_w * 0.5f;
        const float y = m.y * tile_h + tile_h * 0.5f;
        drawUiText(ui_font, m.name, x + 12, y - 12, 13, ORANGE);
    }

    for (const auto& pv : player_visuals) {
        const auto& p = *pv.msg;
        const float x = pv.rx * tile_w + tile_w * 0.5f;
        const float y = pv.ry * tile_h + tile_h * 0.5f;
        Rectangle player_bar{x - (tile_w / 2.0f), y - tile_h - 8.0f, tile_w, 5.0f};
        drawHealthBar(player_bar.x, player_bar.y, player_bar.width, p.hp, p.max_hp);
        const float name_w = MeasureTextEx(ui_font, p.user.c_str(), 13.0f, 1.0f).x;
        drawUiText(ui_font, p.user, x - (name_w * 0.5f), player_bar.y - 14.0f, 13, RAYWHITE);
        if (p.user == game_state.your_user && game_state.attack_target_monster_id >= 0) {
            const float r = 14.0f + 3.0f * std::sin(scene_state.attack_fx_t * 10.0f);
            DrawRing(Vector2{x, y - tile_h * 0.3f}, r, r + 2.5f, 0, 360, 40, Fade(ORANGE, 0.85f));
        }
    }

    if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
        if (mouse.x < GetScreenWidth() - cfg.inventory_panel_width) {
            int target = -1;
            for (auto it = monster_click_boxes.rbegin(); it != monster_click_boxes.rend(); ++it) {
                if (CheckCollisionPointRec(mouse, it->second)) {
                    target = it->first;
                    break;
                }
            }
            out.attack_click = AttackMsg{target};
        }
    }

    drawUiText(ui_font,
               TextFormat("User: %s  Room: %s  Pos: (%d,%d)  HP: %d/%d  Exp: %d",
                          game_state.your_user.c_str(),
                          game_state.your_room.c_str(),
                          game_state.your_x,
                          game_state.your_y,
                          game_state.your_hp,
                          game_state.your_max_hp,
                          game_state.your_exp),
               10,
               10,
               17,
               WHITE);
}

} // namespace client
