#include "client_scene_renderer.h"

#include "combat_fx.h"
#include "speech_bubble.h"
#include "string_util.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "rlgl.h"

#ifndef DEG2RAD
#define DEG2RAD (PI / 180.0f)
#endif
#ifndef RAD2DEG
#define RAD2DEG (180.0f / PI)
#endif

namespace client {
namespace {

constexpr float kCombatFxDurationSec = 0.95f;
constexpr float kCombatFxOpacity = 0.60f;
constexpr float kCombatFxYOffsetPx = 5.0f;
constexpr int kCombatOutcomeHit = 1;

struct ResolvedSheet {
    const Sprites* sprites;
    Texture2D tex;
};

ResolvedSheet resolveSheet(const std::function<SpriteSheetView(const std::string&)>& view_fn,
                           const std::string& tileset,
                           const Sprites& fallback, Texture2D fallback_tex) {
    SpriteSheetView sheet_view{};
    if (view_fn) sheet_view = view_fn(tileset);
    const Sprites* s = sheet_view.sprites ? sheet_view.sprites : &fallback;
    const Texture2D t = (sheet_view.texture.id != 0) ? sheet_view.texture : fallback_tex;
    return {s, t};
}

float updateAttackTimer(SceneState& state, const std::string& key, uint32_t current_seq, float dt, float duration = 0.35f) {
    auto& timer = state.attack_fx_timer_by_key[key];
    auto last_seq_it = state.last_attack_seq_by_key.find(key);
    if (last_seq_it == state.last_attack_seq_by_key.end()) {
        state.last_attack_seq_by_key[key] = current_seq;
        timer = 0.0f;
    } else if (last_seq_it->second != current_seq) {
        last_seq_it->second = current_seq;
        timer = duration;
    } else {
        timer = std::max(0.0f, timer - dt);
    }
    return timer;
}

void updateCombatOutcomeFxState(SceneState& state, const std::string& key,
                                uint32_t current_seq, int outcome, int value, float dt,
                                float delay = 0.0f) {
    auto& fx = state.combat_outcome_fx_by_key[key];
    auto last_it = state.last_combat_outcome_seq_by_key.find(key);
    if (last_it == state.last_combat_outcome_seq_by_key.end()) {
        state.last_combat_outcome_seq_by_key[key] = current_seq;
        fx.timer = 0.0f;
        fx.delay = 0.0f;
        fx.outcome = 0;
        fx.value = 0;
    } else if (last_it->second != current_seq) {
        last_it->second = current_seq;
        fx.outcome = outcome;
        fx.value = value;
        fx.delay = (outcome == 0) ? 0.0f : delay;
        fx.timer = (outcome == 0) ? 0.0f : kCombatFxDurationSec;
    } else if (fx.delay > 0.0f) {
        fx.delay = std::max(0.0f, fx.delay - dt);
    } else {
        fx.timer = std::max(0.0f, fx.timer - dt);
    }
}

const Frame* findFrameForItem(const Sprites& sprites, const std::string& sprite_name, ClipKind kind) {
    const Frame* fr = sprites.frame(sprite_name, Dir::S, 0, kind);
    if (!fr && kind == ClipKind::Death) fr = sprites.frame(sprite_name, Dir::S, 0, ClipKind::Move);
    if (!fr) fr = sprites.frame(sprite_name, Dir::W, 0, ClipKind::Move);
    if (!fr) fr = sprites.frame(sprite_name, Dir::S, 0, ClipKind::Move);
    return fr;
}

// Draws the combat outcome effect (hit/miss/block splat sprite) and, for hits,
// a red damage number that floats upward and fades out.
void drawCombatOutcomeFx(const SceneState& state, const std::string& key,
                         float feet_x, float feet_y,
                         Texture2D combat_fx_tex,
                         Font ui_font, float tile_w, float tile_h) {
    auto it = state.combat_outcome_fx_by_key.find(key);
    if (it == state.combat_outcome_fx_by_key.end()) return;
    const SceneState::CombatOutcomeFx& fx = it->second;
    if (fx.timer <= 0.0f || fx.outcome <= 0 || fx.delay > 0.0f) return;

    const CombatFxAtlas& atlas = combatFxAtlas();
    const float t = std::clamp(fx.timer / kCombatFxDurationSec, 0.0f, 1.0f);
    const float progress = 1.0f - t; // 0 = just started, 1 = fully elapsed

    // Pick the frame sequence for the outcome type (hit/miss/block)
    const std::vector<int>* frames = nullptr;
    if (fx.outcome == 1) frames = &atlas.hit_frames;
    else if (fx.outcome == 2) frames = &atlas.wiff_frames;
    else if (fx.outcome == 3) frames = &atlas.block_frames;
    if (!frames || frames->empty()) return;

    // Draw the splat sprite from the combat FX atlas
    const int idx = std::clamp(static_cast<int>(progress * static_cast<float>(frames->size())),
                               0, static_cast<int>(frames->size()) - 1);
    const float sz = std::max(tile_w, tile_h) * 1.1f;
    const float px = feet_x - sz * 0.5f;
    const float py = feet_y - tile_h * 1.40f + kCombatFxYOffsetPx;
    Color tint = WHITE;
    tint.a = static_cast<unsigned char>(255.0f * kCombatFxOpacity);
    drawAtlasTile(combat_fx_tex, (*frames)[static_cast<size_t>(idx)],
                  atlas.columns, atlas.tile_w, atlas.tile_h,
                  Rectangle{px, py, sz, sz}, tint);

    // Floating damage number (rises 34px, fades out over the duration)
    if (fx.outcome == kCombatOutcomeHit && fx.value > 0) {
        drawFloatingText(ui_font, std::to_string(fx.value),
                         feet_x, py - 2.0f,
                         Color{235, 48, 48, 255}, progress, 34.0f);
    }
}

float baseDegForDir(Dir d) {
    switch (d) {
        case Dir::S: return 270.0f;
        case Dir::N: return 90.0f;
        case Dir::E: return 0.0f;
        case Dir::W: return 180.0f;
        default: return 270.0f;
    }
}

const EquippedItemMsg* findWeaponEquipment(const std::vector<EquippedItemMsg>& eq) {
    for (const auto& e : eq) {
        if (e.equip_type == ItemType::Weapon && e.instance_id > 0) return &e;
    }
    return nullptr;
}

void drawItemSwing(float center_x, float center_y,
                   Dir facing, float progress,
                   const SwingDef& swing,
                   Rectangle src_rect, Texture2D tex,
                   float tile_w, float tile_h, float scale) {
    const float base_deg = baseDegForDir(facing);
    const float start_deg = base_deg + swing.start_offset_degrees;
    const float end_deg = start_deg + swing.arc_degrees;
    const float current_deg = start_deg + (end_deg - start_deg) * progress;
    const float rad = current_deg * DEG2RAD;

    const float radius_px = swing.radius_tiles * tile_w;
    const float ix = center_x + std::cos(rad) * radius_px;
    const float iy = center_y - std::sin(rad) * radius_px;

    const float item_w = src_rect.width * scale;
    const float item_h = src_rect.height * scale;
    const float rotation = -(current_deg - 90.0f);

    Rectangle dst{ix, iy, item_w, item_h};
    Vector2 origin{item_w * 0.5f, item_h * 0.85f};
    DrawTexturePro(tex, src_rect, dst, origin, rotation, WHITE);
}

} // namespace

std::string pickSpriteName(const Sprites& sprites, const MonsterStateMsg& m) {
    const std::string requested = m.sprite_name;
    const std::string req_lower = toLowerAscii(requested);
    const std::string name_lower = toLowerAscii(m.name);

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

std::string pickSpriteName(const Sprites& sprites, const NpcStateMsg& n) {
    const std::string requested = n.sprite_name;
    const std::string req_lower = toLowerAscii(requested);
    const std::string name_lower = toLowerAscii(n.name);

    const auto has_any = [&](const std::string& k) {
        return sprites.frame_count(k, Dir::S) > 0 ||
               sprites.frame_count(k, Dir::E) > 0 ||
               sprites.frame_count(k, Dir::N) > 0 ||
               sprites.frame_count(k, Dir::W) > 0;
    };

    if (!requested.empty() && has_any(requested)) return requested;
    if (!req_lower.empty() && has_any(req_lower)) return req_lower;
    if (!name_lower.empty() && has_any(name_lower)) return name_lower;
    if (has_any("player_1")) return "player_1";
    return requested.empty() ? "player_1" : requested;
}

Dir dirFromFacingInt(int facing, Dir fallback = Dir::S) {
    switch (facing) {
        case 0: return Dir::N;
        case 1: return Dir::E;
        case 2: return Dir::S;
        case 3: return Dir::W;
        default: return fallback;
    }
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
               Texture2D combat_fx_tex,
               Texture2D speech_tex,
               const std::function<SpriteSheetView(const std::string&)>& monster_sheet_view,
               const std::function<ItemSheetView(const std::string&)>& item_sheet_view,
               Font ui_font,
               float dt,
               SceneState& scene_state,
               const SceneConfig& cfg,
               SceneOutput& out) {
    out.attack_click.reset();
    out.move_ground_item.reset();
    out.pickup_ground_item.reset();
    scene_state.attack_fx_t += dt;

    // Track XP gain events
    constexpr float kXpFxDurationSec = 1.2f;
    if (game_state.xp_gain_seq != scene_state.last_xp_gain_seq) {
        scene_state.last_xp_gain_seq = game_state.xp_gain_seq;
        scene_state.xp_gain_amount = game_state.xp_gain_amount;
        scene_state.xp_gain_timer = (game_state.xp_gain_amount > 0) ? kXpFxDurationSec : 0.0f;
    } else {
        scene_state.xp_gain_timer = std::max(0.0f, scene_state.xp_gain_timer - dt);
    }

    // Compute
    const float tile_w = room.tile_width() * cfg.map_scale;
    const float tile_h = room.tile_height() * cfg.map_scale;

    // Pre-compute local player's swing duration for monster combat FX delay
    float player_swing_duration = 0.35f;
    if (cfg.swing_defs) {
        const EquippedItemMsg* weap = findWeaponEquipment(game_state.your_equipment);
        if (weap) {
            std::string st = weap->swing_type;
            if (st.empty()) st = "sidearm";
            auto sd_it = cfg.swing_defs->find(st);
            if (sd_it != cfg.swing_defs->end()) player_swing_duration = sd_it->second.duration_sec;
        }
    }
    constexpr float kSwingContactFraction = 0.7f;
    const float monster_fx_delay = player_swing_duration * kSwingContactFraction;

    std::vector<std::pair<int, Rectangle>> monster_click_boxes;
    struct MonsterVisual {
        const MonsterStateMsg* msg = nullptr;
        const Sprites* sprites = nullptr;
        Texture2D tex{};
        AnimationComponent* anim = nullptr;
        float rx = 0.0f;
        float ry = 0.0f;
        Rectangle click_box{};
    };
    struct PlayerVisual {
        const PlayerStateMsg* msg = nullptr;
        float rx = 0.0f;
        float ry = 0.0f;
        AnimationComponent* anim = nullptr;
    };
    struct NpcVisual {
        const NpcStateMsg* msg = nullptr;
        const Sprites* sprites = nullptr;
        Texture2D tex{};
        AnimationComponent* anim = nullptr;
        float rx = 0.0f;
        float ry = 0.0f;
    };
    struct DrawCmd {
        float feet_y = 0.0f;
        int kind = 0; // 0 monster, 1 npc, 2 player
        size_t idx = 0;
    };
    std::vector<MonsterVisual> monster_visuals;
    std::vector<NpcVisual> npc_visuals;
    std::vector<PlayerVisual> player_visuals;
    std::vector<DrawCmd> draw_cmds;
    const MonsterStateMsg* active_target = nullptr;
    for (const auto& m : game_state.monsters) {
        if (m.id == game_state.attack_target_monster_id) {
            active_target = &m;
            break;
        }
    }
    const bool target_in_range = active_target &&
                                 (std::abs(active_target->x - game_state.your_x) +
                                  std::abs(active_target->y - game_state.your_y) <= 1);

    struct GroundItemVisual {
        const GroundItemStateMsg* msg = nullptr;
        const Sprites* sprites = nullptr;
        Texture2D tex{};
        Rectangle tile_box{};
    };
    std::vector<GroundItemVisual> ground_items;
    ground_items.reserve(game_state.items.size());
    for (const auto& item : game_state.items) {
        ItemSheetView sheet_view{};
        if (item_sheet_view) sheet_view = item_sheet_view(item.sprite_tileset);
        ground_items.push_back(GroundItemVisual{
            &item,
            sheet_view.sprites,
            sheet_view.texture,
            Rectangle{item.x * tile_w, item.y * tile_h, tile_w, tile_h}
        });
    }

    const auto itemClipKind = [](const GroundItemStateMsg& item) {
        return (item.sprite_clip == 1) ? ClipKind::Death : ClipKind::Move;
    };

    for (const auto& m : game_state.monsters) {
        const auto [mon_sprites, mon_tex] = resolveSheet(monster_sheet_view, m.sprite_tileset, sprites, character_tex);

        const std::string key = "m:" + std::to_string(m.id);
        auto& anim = scene_state.anim_by_key[key];
        anim.sprite_name = pickSpriteName(*mon_sprites, m);

        auto& prev = scene_state.prev_pos_by_key[key];
        const int dx = m.x - prev.first;
        const int dy = m.y - prev.second;
        const bool moved = (dx != 0 || dy != 0);
        anim.dir = dirFromFacingInt(m.facing, anim.dir);
        const float attack_t = updateAttackTimer(scene_state, key, m.attack_anim_seq, dt);
        updateCombatOutcomeFxState(scene_state, key, m.combat_outcome_seq, m.combat_outcome, m.combat_value, dt, monster_fx_delay);
        const bool action_active = target_in_range && (m.id == game_state.attack_target_monster_id);
        tickAnimation(anim, *mon_sprites, moved, action_active || attack_t > 0.0f, dt);
        prev = {m.x, m.y};
        const auto [rx, ry] = smoothPos(scene_state.render_pos_by_key,
                                        key, m.x, m.y, dt,
                                        cfg.monster_slide_tiles_per_sec);

        const float mw = std::max(1, m.sprite_w_tiles) * tile_w;
        const float mh = std::max(1, m.sprite_h_tiles) * tile_h;
        const float cell_x = rx * tile_w;
        const float cell_y = (ry - (std::max(1, m.sprite_h_tiles) - 1)) * tile_h;
        Rectangle click_box{cell_x, cell_y, mw, mh};

        monster_click_boxes.push_back({m.id, click_box});
        monster_visuals.push_back(MonsterVisual{
            &m, mon_sprites, mon_tex, &anim, rx, ry, click_box
        });
        draw_cmds.push_back(DrawCmd{ry * tile_h, 0, monster_visuals.size() - 1});
    }

    for (const auto& n : game_state.npcs) {
        const auto [npc_sprites, npc_tex] = resolveSheet(monster_sheet_view, n.sprite_tileset, sprites, character_tex);

        const std::string key = "n:" + std::to_string(n.id);
        auto& anim = scene_state.anim_by_key[key];
        anim.sprite_name = pickSpriteName(*npc_sprites, n);

        auto& prev = scene_state.prev_pos_by_key[key];
        const int dx = n.x - prev.first;
        const int dy = n.y - prev.second;
        const bool moved = (dx != 0 || dy != 0);
        anim.dir = dirFromFacingInt(n.facing, anim.dir);
        tickAnimation(anim, *npc_sprites, moved, false, dt);
        prev = {n.x, n.y};
        const auto [rx, ry] = smoothPos(scene_state.render_pos_by_key,
                                        key, n.x, n.y, dt,
                                        cfg.monster_slide_tiles_per_sec);
        npc_visuals.push_back(NpcVisual{
            &n, npc_sprites, npc_tex, &anim, rx, ry
        });
        draw_cmds.push_back(DrawCmd{ry * tile_h, 1, npc_visuals.size() - 1});
    }

    for (const auto& p : game_state.players) {
        const std::string key = "p:" + p.user;
        auto& anim = scene_state.anim_by_key[key];
        anim.sprite_name = "player_1";

        auto& prev = scene_state.prev_pos_by_key[key];
        const int dx = p.x - prev.first;
        const int dy = p.y - prev.second;
        anim.dir = dirFromFacingInt(p.facing, anim.dir);
        float swing_duration = 0.35f;
        if (cfg.swing_defs) {
            const auto& eq = (p.user == game_state.your_user)
                ? game_state.your_equipment : p.equipment;
            const EquippedItemMsg* weap = findWeaponEquipment(eq);
            if (weap) {
                std::string st = weap->swing_type;
                if (st.empty()) st = "sidearm";
                auto sd_it = cfg.swing_defs->find(st);
                if (sd_it != cfg.swing_defs->end()) swing_duration = sd_it->second.duration_sec;
            }
        }
        const float attack_t = updateAttackTimer(scene_state, key, p.attack_anim_seq, dt, swing_duration);
        updateCombatOutcomeFxState(scene_state, key, p.combat_outcome_seq, p.combat_outcome, p.combat_value, dt);
        tickAnimation(anim, sprites, (dx != 0 || dy != 0), false, dt);
        prev = {p.x, p.y};
        const auto [rx, ry] = smoothPos(scene_state.render_pos_by_key,
                                        key, p.x, p.y, dt,
                                        cfg.player_slide_tiles_per_sec);

        player_visuals.push_back(PlayerVisual{&p, rx, ry, &anim});
        draw_cmds.push_back(DrawCmd{ry * tile_h, 2, player_visuals.size() - 1});
    }

    auto findTopItemAt = [&](Vector2 mouse) -> int {
        for (auto it = ground_items.rbegin(); it != ground_items.rend(); ++it) {
            if (CheckCollisionPointRec(mouse, it->tile_box)) return it->msg->id;
        }
        return -1;
    };

    const float map_ox = cfg.map_origin_x;
    const float map_oy = cfg.map_origin_y;
    const float map_vw = cfg.map_view_width > 0.0f ? cfg.map_view_width : static_cast<float>(GetScreenWidth());
    const float map_vh = cfg.map_view_height > 0.0f ? cfg.map_view_height : static_cast<float>(GetScreenHeight());
    const Vector2 mouse = GetMousePosition();
    const Vector2 mouse_local{mouse.x - map_ox, mouse.y - map_oy};
    const bool mouse_in_map = mouse.x >= map_ox &&
                              mouse.y >= map_oy &&
                              mouse.x < (map_ox + map_vw) &&
                              mouse.y < (map_oy + map_vh);
    const int hovered_item_id = mouse_in_map ? findTopItemAt(mouse_local) : -1;

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && scene_state.dragging_ground_item_id < 0 && hovered_item_id >= 0) {
        scene_state.dragging_ground_item_id = hovered_item_id;
    }
    if (scene_state.dragging_ground_item_id >= 0 && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        if (mouse_in_map) {
            const int tx = static_cast<int>(std::floor(mouse_local.x / tile_w));
            const int ty = static_cast<int>(std::floor(mouse_local.y / tile_h));
            out.move_ground_item = MoveGroundItemMsg{scene_state.dragging_ground_item_id, tx, ty};
        } else {
            out.pickup_ground_item = PickupMsg{scene_state.dragging_ground_item_id};
        }
        scene_state.dragging_ground_item_id = -1;
    }
    if (scene_state.dragging_ground_item_id >= 0 || hovered_item_id >= 0) {
        SetMouseCursor(MOUSE_CURSOR_CROSSHAIR);
    } else {
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    }

    rlPushMatrix();
    rlTranslatef(map_ox, map_oy, 0.0f);

    // Ground-space attack marker
    for (const auto& mv : monster_visuals) {
        const auto& m = *mv.msg;
        if (game_state.attack_target_monster_id != m.id) continue;
        const float mw = std::max(1, m.sprite_w_tiles) * tile_w;
        Rectangle feet_box{mv.rx * tile_w, mv.ry * tile_h, mw, tile_h};
        DrawRectangleRec(feet_box, Fade(RED, 0.22f));
        DrawRectangleLinesEx(feet_box, 2.0f, RED);
    }

    for (const auto& itv : ground_items) {
        if (itv.msg->id == scene_state.dragging_ground_item_id) continue;
        if (!itv.sprites) continue;
        const ClipKind kind = itemClipKind(*itv.msg);
        const Frame* fr = findFrameForItem(*itv.sprites, itv.msg->sprite_name, kind);
        if (!fr) continue;
        const Rectangle src = fr->rect();
        Rectangle dst{itv.tile_box.x, itv.tile_box.y, src.width * cfg.map_scale, src.height * cfg.map_scale};
        dst.y += tile_h - dst.height;
        DrawTexturePro(itv.tex, src, dst, Vector2{0, 0}, 0.0f, WHITE);
    }

    std::sort(draw_cmds.begin(), draw_cmds.end(), [](const DrawCmd& a, const DrawCmd& b) {
        if (a.feet_y != b.feet_y) return a.feet_y < b.feet_y;
        return a.kind < b.kind;
    });

    for (const auto& cmd : draw_cmds) {
        if (cmd.kind == 0) {
            const auto& mv = monster_visuals[cmd.idx];
            drawActor(*mv.sprites, mv.tex, *mv.anim, mv.rx, mv.ry, tile_w, tile_h, cfg.map_scale, WHITE);
        } else if (cmd.kind == 1) {
            const auto& nv = npc_visuals[cmd.idx];
            drawActor(*nv.sprites, nv.tex, *nv.anim, nv.rx, nv.ry, tile_w, tile_h, cfg.map_scale, WHITE);
        } else {
            const auto& pv = player_visuals[cmd.idx];
            const auto& p = *pv.msg;
            const Color tint = (p.user == game_state.your_user) ? WHITE : LIGHTGRAY;

            // Resolve weapon swing parameters once for before/after draw
            auto tryDrawSwing = [&]() {
                const std::string pkey = "p:" + p.user;
                auto timer_it = scene_state.attack_fx_timer_by_key.find(pkey);
                if (timer_it == scene_state.attack_fx_timer_by_key.end() || timer_it->second <= 0.0f || !cfg.swing_defs) return;
                const auto& eq = (p.user == game_state.your_user)
                    ? game_state.your_equipment : p.equipment;
                const EquippedItemMsg* weapon = findWeaponEquipment(eq);
                if (!weapon || weapon->sprite_tileset.empty() || !item_sheet_view) return;
                ItemSheetView sheet = item_sheet_view(weapon->sprite_tileset);
                if (!sheet.sprites || sheet.texture.id == 0) return;
                const Frame* fr = findFrameForItem(*sheet.sprites, weapon->sprite_name, ClipKind::Move);
                if (!fr) return;
                std::string st = weapon->swing_type;
                if (st.empty()) st = "sidearm";
                auto sd_it = cfg.swing_defs->find(st);
                SwingDef swing;
                if (sd_it != cfg.swing_defs->end()) swing = sd_it->second;
                const float progress = 1.0f - (timer_it->second / swing.duration_sec);
                const float cx = pv.rx * tile_w + tile_w * 0.5f;
                const float cy = pv.ry * tile_h + tile_h * 0.5f;
                drawItemSwing(cx, cy, pv.anim->dir,
                              std::clamp(progress, 0.0f, 1.0f),
                              swing, fr->rect(), sheet.texture,
                              tile_w, tile_h, cfg.map_scale);
            };

            // Draw swing under character for N/E/W, over for S
            if (pv.anim->dir != Dir::S) tryDrawSwing();
            drawActor(sprites, character_tex, *pv.anim, pv.rx, pv.ry, tile_w, tile_h, cfg.map_scale, tint);
            if (pv.anim->dir == Dir::S) tryDrawSwing();
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
        if (drag_item && drag_item->sprites) {
            const ClipKind kind = itemClipKind(*drag_item->msg);
            const Frame* fr = findFrameForItem(*drag_item->sprites, drag_item->msg->sprite_name, kind);
            if (fr) {
                const Rectangle src = fr->rect();
                Rectangle dst{mouse_local.x - (src.width * cfg.map_scale * 0.5f),
                              mouse_local.y - (src.height * cfg.map_scale * 0.75f),
                              src.width * cfg.map_scale,
                              src.height * cfg.map_scale};
                DrawTexturePro(drag_item->tex, src, dst, Vector2{0, 0}, 0.0f, Fade(WHITE, 0.85f));
            }
        }
    }

    for (const auto& mv : monster_visuals) {
        const auto& m = *mv.msg;
        const float bar_y = mv.click_box.y + (tile_h * 0.5f) - 10.0f;
        drawHealthBar(mv.click_box.x, bar_y, mv.click_box.width, m.hp, m.max_hp);
        drawCombatOutcomeFx(scene_state, "m:" + std::to_string(m.id),
                            mv.rx * tile_w + mv.click_box.width * 0.5f,
                            mv.ry * tile_h + tile_h * 0.5f,
                            combat_fx_tex, ui_font, tile_w, tile_h);
        const float label_size = cfg.monster_name_text_size * uiScreenScale();
        const float name_w = MeasureTextEx(ui_font, m.name.c_str(), label_size, 1.0f).x;
        drawUiText(ui_font, m.name, mv.click_box.x + (mv.click_box.width - name_w) * 0.5f, bar_y - (label_size + 3.0f), label_size, ORANGE);
    }

    for (const auto& pv : player_visuals) {
        const auto& p = *pv.msg;
        const float x = pv.rx * tile_w + tile_w * 0.5f;
        const float y = pv.ry * tile_h + tile_h * 0.5f;
        Rectangle player_bar{x - (tile_w / 2.0f), y - tile_h - 10.0f, tile_w, 5.0f};
        drawHealthBar(player_bar.x, player_bar.y, player_bar.width, p.hp, p.max_hp);
        drawCombatOutcomeFx(scene_state, "p:" + p.user, x, y,
                            combat_fx_tex, ui_font, tile_w, tile_h);

        // Floating "+N exp" text when your player gains XP
        if (p.user == game_state.your_user && scene_state.xp_gain_timer > 0.0f) {
            const float progress = 1.0f - std::clamp(scene_state.xp_gain_timer / kXpFxDurationSec, 0.0f, 1.0f);
            drawFloatingText(ui_font, "+" + std::to_string(scene_state.xp_gain_amount) + " exp",
                             x, y - tile_h * 1.2f, Color{255, 255, 255, 255}, progress, 40.0f);
        }

        const float label_size = cfg.player_name_text_size * uiScreenScale();
        const float name_w = MeasureTextEx(ui_font, p.user.c_str(), label_size, 1.0f).x;
        drawUiText(ui_font, p.user, x - (name_w * 0.5f), player_bar.y - (label_size + 1.0f), label_size, RAYWHITE);
    }

    for (const auto& nv : npc_visuals) {
        const auto& n = *nv.msg;
        const float x = nv.rx * tile_w + tile_w * 0.5f;
        const float y = nv.ry * tile_h + tile_h * 0.5f;
        const float label_size = cfg.npc_name_text_size * uiScreenScale();
        const float name_w = MeasureTextEx(ui_font, n.name.c_str(), label_size, 1.0f).x;
        drawUiText(ui_font, n.name, x - (name_w * 0.5f), y - tile_h - (label_size + 2.0f), label_size, Color{210, 232, 255, 255});
    }

    rlPopMatrix();

    if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
        if (mouse_in_map) {
            int target = -1;
            for (auto it = monster_click_boxes.rbegin(); it != monster_click_boxes.rend(); ++it) {
                if (CheckCollisionPointRec(mouse_local, it->second)) {
                    target = it->first;
                    break;
                }
            }
            out.attack_click = AttackMsg{target};
        }
    }

    for (auto it = scene_state.speech_timer_by_user.begin(); it != scene_state.speech_timer_by_user.end();) {
        it->second = std::max(0.0f, it->second - dt);
        if (it->second <= 0.0f) {
            scene_state.speech_text_by_user.erase(it->first);
            scene_state.speech_type_by_user.erase(it->first);
            scene_state.speech_seq_by_user.erase(it->first);
            it = scene_state.speech_timer_by_user.erase(it);
        } else {
            ++it;
        }
    }
}

void drawSpeechOverlays(const Room& room,
                        const GameStateMsg& game_state,
                        Texture2D speech_tex,
                        Font ui_font,
                        Font ui_bold_font,
                        SceneState& scene_state,
                        const SceneConfig& cfg) {
    struct Bubble {
        uint64_t seq = 0;
        std::string text;
        std::string speech_type;
        float head_x = 0.0f;
        float head_y = 0.0f;
    };

    const float tile_w = room.tile_width() * cfg.map_scale;
    const float tile_h = room.tile_height() * cfg.map_scale;
    std::vector<Bubble> bubbles;

    auto pushBubbleFor = [&](const std::string& who, float rx, float ry) {
        auto sit = scene_state.speech_text_by_user.find(who);
        auto yit = scene_state.speech_type_by_user.find(who);
        auto tit = scene_state.speech_timer_by_user.find(who);
        if (sit == scene_state.speech_text_by_user.end()) return;
        if (tit == scene_state.speech_timer_by_user.end()) return;
        if (tit->second <= 0.0f || sit->second.empty()) return;
        uint64_t seq = 0;
        auto qit = scene_state.speech_seq_by_user.find(who);
        if (qit != scene_state.speech_seq_by_user.end()) seq = qit->second;
        bubbles.push_back(Bubble{
            seq,
            sit->second,
            (yit != scene_state.speech_type_by_user.end()) ? yit->second : std::string("talk"),
            cfg.map_origin_x + (rx * tile_w + tile_w * 0.5f) + tile_w * 0.20f,
            cfg.map_origin_y + (ry * tile_h + tile_h * 0.5f) - tile_h * 0.68f
        });
    };

    for (const auto& p : game_state.players) {
        const std::string key = "p:" + p.user;
        auto rit = scene_state.render_pos_by_key.find(key);
        const float rx = (rit != scene_state.render_pos_by_key.end()) ? rit->second.first : static_cast<float>(p.x);
        const float ry = (rit != scene_state.render_pos_by_key.end()) ? rit->second.second : static_cast<float>(p.y);
        pushBubbleFor(p.user, rx, ry);
    }
    for (const auto& n : game_state.npcs) {
        const std::string key = "n:" + std::to_string(n.id);
        auto rit = scene_state.render_pos_by_key.find(key);
        const float rx = (rit != scene_state.render_pos_by_key.end()) ? rit->second.first : static_cast<float>(n.x);
        const float ry = (rit != scene_state.render_pos_by_key.end()) ? rit->second.second : static_cast<float>(n.y);
        pushBubbleFor(n.name, rx, ry);
    }

    std::sort(bubbles.begin(), bubbles.end(), [](const Bubble& a, const Bubble& b) {
        return a.seq < b.seq;
    });

    for (const auto& b : bubbles) {
        drawTalkBubble(speech_tex, ui_font, ui_bold_font, b.speech_type, b.text,
                       b.head_x, b.head_y, cfg.map_scale, cfg.map_view_width,
                       cfg.speech_text_size, cfg.speech_bubble_alpha);
    }
}

} // namespace client
