#include "client_scene_renderer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <vector>

#include <tinyxml2.h>
#include "rlgl.h"

namespace client {
namespace {

constexpr int kSpeechCols = 9;
constexpr float kSpeechTilePx = 16.0f;
constexpr float kSpeechTextPadX = 8.0f;
constexpr float kSpeechTextPadY = 6.0f;
constexpr float kUiBaseW = 1200.0f;
constexpr float kUiBaseH = 760.0f;
constexpr float kCombatFxDurationSec = 0.95f;
constexpr float kCombatFxOpacity = 0.60f;
constexpr float kCombatFxYOffsetPx = 5.0f;
constexpr int kCombatOutcomeHit = 1;

float uiScreenScale() {
    const float sx = static_cast<float>(GetScreenWidth()) / kUiBaseW;
    const float sy = static_cast<float>(GetScreenHeight()) / kUiBaseH;
    return std::max(0.85f, std::min(2.2f, std::min(sx, sy)));
}

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

Rectangle speechSrcRect(int tile_id) {
    const int tx = tile_id % kSpeechCols;
    const int ty = tile_id / kSpeechCols;
    return Rectangle{
        tx * kSpeechTilePx,
        ty * kSpeechTilePx,
        kSpeechTilePx,
        kSpeechTilePx
    };
}

int speechTileId(const std::string& raw_type, char pos) {
    const std::string type = lowerCopy(raw_type);
    if (type == "think") {
        switch (pos) {
            case 'a': return 15;
            case 'b': return 17;
            case 'c': return 24;
            case 'd': return 6;
            case 'e': return 7;
            case 'f': return 8;
            case 'g': return 16;
            case 'h': return 25;
            case 'i': return 40;
            case 'j': return 49;
            case 't': return 26;
            default: return 16;
        }
    }
    if (type == "yell") {
        switch (pos) {
            case 'a': return 36;
            case 'b': return 38;
            case 'c': return 45;
            case 'd': return 27;
            case 'e': return 28;
            case 'f': return 29;
            case 'g': return 37;
            case 'h': return 46;
            case 'i': return 22;
            case 'j': return 31;
            case 't': return 47;
            default: return 37;
        }
    }
    // Default talk.
    switch (pos) {
        case 'a': return 9;
        case 'b': return 11;
        case 'c': return 18;
        case 'd': return 0;
        case 'e': return 1;
        case 'f': return 2;
        case 'g': return 10;
        case 'h': return 19;
        case 'i': return 4;
        case 'j': return 13;
        case 't': return 20;
        default: return 10;
    }
}

void drawSpeechTile(Texture2D tex, int tile_id, float x, float y, float size_px, float bubble_alpha) {
    const Rectangle src = speechSrcRect(tile_id);
    const Rectangle dst{x, y, size_px, size_px};
    DrawTexturePro(tex, src, dst, Vector2{0, 0}, 0.0f, Fade(WHITE, bubble_alpha));
}

std::vector<std::string> wrapSpeechText(Font font,
                                        const std::string& text,
                                        float font_size,
                                        float max_text_width) {
    std::vector<std::string> out;
    if (text.empty()) return out;
    if (max_text_width <= 8.0f) {
        out.push_back(text);
        return out;
    }

    std::istringstream iss(text);
    std::string word;
    std::string line;
    while (iss >> word) {
        std::string candidate = line.empty() ? word : (line + " " + word);
        const float w = MeasureTextEx(font, candidate.c_str(), font_size, 1.0f).x;
        if (w <= max_text_width || line.empty()) {
            line = std::move(candidate);
        } else {
            out.push_back(line);
            line = word;
        }
    }
    if (!line.empty()) out.push_back(std::move(line));
    if (out.empty()) out.push_back(text);
    return out;
}

void drawTalkBubble(Texture2D speech_tex,
                    Font ui_font,
                    const std::string& speech_type,
                    const std::string& text,
                    float head_x,
                    float head_y,
                    float map_scale,
                    float map_view_width,
                    float speech_text_size,
                    float bubble_alpha) {
    if (speech_tex.id == 0 || text.empty()) return;

    const float ui_scale = uiScreenScale();
    const float tile_px = std::max(12.0f, std::round(map_scale * 16.0f));
    const float font_size = std::max(10.0f, std::round(speech_text_size * ui_scale));
    const float max_text_width = std::max(tile_px * 4.0f, map_view_width * 0.42f);
    const std::vector<std::string> lines = wrapSpeechText(ui_font, text, font_size, max_text_width);

    float max_line_w = 0.0f;
    for (const auto& line : lines) {
        max_line_w = std::max(max_line_w, MeasureTextEx(ui_font, line.c_str(), font_size, 1.0f).x);
    }
    const float line_h = MeasureTextEx(ui_font, "Ag", font_size, 1.0f).y;
    const float text_block_h = line_h * static_cast<float>(lines.size()) + std::max(0.0f, static_cast<float>(lines.size() - 1)) * 1.0f;

    const float inner_w_px = max_line_w + kSpeechTextPadX * 2.0f;
    const float inner_h_px = text_block_h + kSpeechTextPadY * 2.0f;

    // Compact mode: short single-line text uses the minimal 3x3 bubble (no e/g/h repeats).
    // Compact one-liners should avoid extra repeated middle rows.
    const bool compact_one_liner = (lines.size() == 1 && lines.front().size() <= 14);
    const bool ultra_compact_one_liner =
        (lines.size() == 1 &&
         lines.front().size() <= 6 &&
         max_line_w <= tile_px * 1.25f);

    const int inner_cols = compact_one_liner
        ? 1
        : std::max(1, static_cast<int>(std::ceil(inner_w_px / tile_px)));
    const int mid_rows = ultra_compact_one_liner
        ? 0
        : (compact_one_liner
        ? 1
        : std::max(1, static_cast<int>(std::ceil(inner_h_px / tile_px))));
    const int cols = inner_cols + 2;
    const int rows = 1 + mid_rows + 1;

    const float bubble_w = cols * tile_px;
    const float bubble_h = rows * tile_px;
    const float bubble_x = head_x - bubble_w * 0.5f;
    const float bubble_y = head_y - bubble_h - tile_px * 1.6f;

    // Top: d e...e f
    for (int c = 0; c < cols; ++c) {
        const int id = (c == 0) ? speechTileId(speech_type, 'd')
                                : ((c == cols - 1) ? speechTileId(speech_type, 'f')
                                                   : speechTileId(speech_type, 'e'));
        drawSpeechTile(speech_tex, id, bubble_x + c * tile_px, bubble_y, tile_px, bubble_alpha);
    }

    // Mid rows: a g...g b
    for (int r = 0; r < mid_rows; ++r) {
        const float y = bubble_y + (1 + r) * tile_px;
        for (int c = 0; c < cols; ++c) {
            const int id = (c == 0) ? speechTileId(speech_type, 'a')
                                    : ((c == cols - 1) ? speechTileId(speech_type, 'b')
                                                       : speechTileId(speech_type, 'g'));
            drawSpeechTile(speech_tex, id, bubble_x + c * tile_px, y, tile_px, bubble_alpha);
        }
    }

    // Bottom: c h... i ...h t
    const float bottom_y = bubble_y + (rows - 1) * tile_px;
    const int center_col = cols / 2;
    for (int c = 0; c < cols; ++c) {
        int id = speechTileId(speech_type, 'h');
        if (c == 0) id = speechTileId(speech_type, 'c');
        else if (c == cols - 1) id = speechTileId(speech_type, 't');
        else if (c == center_col) id = speechTileId(speech_type, 'i');
        drawSpeechTile(speech_tex, id, bubble_x + c * tile_px, bottom_y, tile_px, bubble_alpha);
    }

    // Single "j" tail tile; head (O) is the target marker, not another bubble piece.
    const float tail_x = bubble_x + center_col * tile_px;
    const float tail_y = bottom_y + tile_px * 0.88f;
    drawSpeechTile(speech_tex, speechTileId(speech_type, 'j'), tail_x, tail_y, tile_px, bubble_alpha);

    const float text_top = bubble_y + (bubble_h - text_block_h) * 0.5f - tile_px * 0.15f;
    for (size_t i = 0; i < lines.size(); ++i) {
        const float lw = MeasureTextEx(ui_font, lines[i].c_str(), font_size, 1.0f).x;
        const float text_x = bubble_x + (bubble_w - lw) * 0.5f;
        const float text_y = text_top + static_cast<float>(i) * (line_h + 1.0f);
        drawUiText(ui_font, lines[i], text_x, text_y, font_size, BLACK);
    }
}

} // namespace

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

std::string pickSpriteName(const Sprites& sprites, const NpcStateMsg& n) {
    const std::string requested = n.sprite_name;
    const std::string req_lower = toLower(requested);
    const std::string name_lower = toLower(n.name);

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

struct CombatFxAtlas {
    int columns = 40;
    int tile_w = 16;
    int tile_h = 16;
    std::vector<int> hit_frames;
    std::vector<int> wiff_frames;
    std::vector<int> block_frames;
    bool loaded = false;
};

std::vector<int> makeFramesFromSeqPairs(std::vector<std::pair<int, int>> seq_pairs, int fallback_anchor) {
    if (seq_pairs.empty()) return {std::max(0, fallback_anchor)};
    std::sort(seq_pairs.begin(), seq_pairs.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second < b.second;
    });
    std::vector<int> out;
    out.reserve(seq_pairs.size());
    for (const auto& [_, tile_id] : seq_pairs) out.push_back(tile_id);
    return out;
}

const CombatFxAtlas& combatFxAtlas() {
    static CombatFxAtlas atlas{};
    if (atlas.loaded) return atlas;
    atlas.loaded = true;

    tinyxml2::XMLDocument doc;
    if (doc.LoadFile("game/assets/art/properties.tsx") != tinyxml2::XML_SUCCESS) {
        atlas.hit_frames = {0, 1, 2};
        atlas.wiff_frames = {40, 41, 42};
        atlas.block_frames = {80, 81, 82};
        return atlas;
    }

    const tinyxml2::XMLElement* root = doc.FirstChildElement("tileset");
    if (!root) return atlas;
    if (const char* c = root->Attribute("columns")) atlas.columns = std::max(1, std::atoi(c));
    if (const char* tw = root->Attribute("tilewidth")) atlas.tile_w = std::max(1, std::atoi(tw));
    if (const char* th = root->Attribute("tileheight")) atlas.tile_h = std::max(1, std::atoi(th));

    std::vector<std::pair<int, int>> hit_seq_pairs;
    std::vector<std::pair<int, int>> wiff_seq_pairs;
    std::vector<std::pair<int, int>> block_seq_pairs;
    std::string last_desc;
    for (const tinyxml2::XMLElement* tile = root->FirstChildElement("tile");
         tile != nullptr;
         tile = tile->NextSiblingElement("tile")) {
        int tile_id = -1;
        tile->QueryIntAttribute("id", &tile_id);
        if (tile_id < 0) continue;

        std::string desc;
        int seq_idx = -1;
        const tinyxml2::XMLElement* props = tile->FirstChildElement("properties");
        if (props) {
            for (const tinyxml2::XMLElement* p = props->FirstChildElement("property");
                 p != nullptr;
                 p = p->NextSiblingElement("property")) {
                const char* name = p->Attribute("name");
                const char* value = p->Attribute("value");
                if (!name || !value) continue;
                std::string n = name;
                std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                if (n == "description") desc = value;
                else if (n == "seq") {
                    try { seq_idx = std::stoi(value); } catch (...) {}
                } else if (n == "sequence") {
                    // Backward compatibility: allow sequence index or count.
                    try { seq_idx = std::stoi(value); } catch (...) {}
                }
            }
        }
        if (!desc.empty()) {
            std::transform(desc.begin(), desc.end(), desc.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            last_desc = desc;
        } else if (seq_idx >= 0 && !last_desc.empty()) {
            desc = last_desc;
        }
        if (desc.empty()) continue;
        std::string key = desc;
        if (seq_idx < 0) seq_idx = static_cast<int>(tile_id);
        if (key == "hit") hit_seq_pairs.push_back({seq_idx, tile_id});
        else if (key == "wiff" || key == "whiff" || key == "miss") wiff_seq_pairs.push_back({seq_idx, tile_id});
        else if (key == "block" || key == "blocked") block_seq_pairs.push_back({seq_idx, tile_id});
    }

    atlas.hit_frames = makeFramesFromSeqPairs(std::move(hit_seq_pairs), 0);
    atlas.wiff_frames = makeFramesFromSeqPairs(std::move(wiff_seq_pairs), 40);
    atlas.block_frames = makeFramesFromSeqPairs(std::move(block_seq_pairs), 80);
    return atlas;
}

void drawScene(const Room& room,
               const GameStateMsg& game_state,
               const Sprites& sprites,
               Texture2D character_tex,
               bool sprite_ready,
               Texture2D combat_fx_tex,
               bool combat_fx_ready,
               Texture2D speech_tex,
               bool speech_ready,
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

    const float tile_w = room.tile_width() * cfg.map_scale;
    const float tile_h = room.tile_height() * cfg.map_scale;
    std::vector<std::pair<int, Rectangle>> monster_click_boxes;
    struct MonsterVisual {
        const MonsterStateMsg* msg = nullptr;
        const Sprites* sprites = nullptr;
        Texture2D tex{};
        bool ready = false;
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
        bool ready = false;
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

    const auto itemClipKind = [](const GroundItemStateMsg& item) {
        return (item.sprite_clip == 1) ? ClipKind::Death : ClipKind::Move;
    };

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
        const bool moved = (dx != 0 || dy != 0);
        anim.dir = dirFromFacingInt(m.facing, anim.dir);
        auto& attack_t = scene_state.attack_fx_timer_by_key[key];
        auto last_seq_it = scene_state.last_attack_seq_by_key.find(key);
        if (last_seq_it == scene_state.last_attack_seq_by_key.end()) {
            scene_state.last_attack_seq_by_key[key] = m.attack_anim_seq;
            attack_t = 0.0f;
        } else if (last_seq_it->second != m.attack_anim_seq) {
            last_seq_it->second = m.attack_anim_seq;
            attack_t = 0.35f;
        } else {
            attack_t = std::max(0.0f, attack_t - dt);
        }
        auto& outcome_fx = scene_state.combat_outcome_fx_by_key[key];
        auto last_outcome_it = scene_state.last_combat_outcome_seq_by_key.find(key);
        if (last_outcome_it == scene_state.last_combat_outcome_seq_by_key.end()) {
            scene_state.last_combat_outcome_seq_by_key[key] = m.combat_outcome_seq;
            outcome_fx.timer = 0.0f;
            outcome_fx.outcome = 0;
            outcome_fx.value = 0;
        } else if (last_outcome_it->second != m.combat_outcome_seq) {
            last_outcome_it->second = m.combat_outcome_seq;
            outcome_fx.outcome = m.combat_outcome;
            outcome_fx.value = m.combat_value;
            outcome_fx.timer = (m.combat_outcome == 0) ? 0.0f : kCombatFxDurationSec;
        } else {
            outcome_fx.timer = std::max(0.0f, outcome_fx.timer - dt);
        }
        const bool action_active = target_in_range && (m.id == game_state.attack_target_monster_id);
        tickAnimation(anim, *mon_sprites, moved, action_active || attack_t > 0.0f, dt);
        prev = {m.x, m.y};
        const auto [rx, ry] = smoothPos(scene_state.render_pos_by_key,
                                        key,
                                        m.x,
                                        m.y,
                                        dt,
                                        cfg.monster_slide_tiles_per_sec);

        const float mw = std::max(1, m.sprite_w_tiles) * tile_w;
        const float mh = std::max(1, m.sprite_h_tiles) * tile_h;
        const float cell_x = rx * tile_w;
        const float cell_y = (ry - (std::max(1, m.sprite_h_tiles) - 1)) * tile_h;
        Rectangle click_box{cell_x, cell_y, mw, mh};

        monster_click_boxes.push_back({m.id, click_box});
        monster_visuals.push_back(MonsterVisual{
            &m, mon_sprites, mon_tex, mon_ready, &anim, rx, ry, click_box
        });
        draw_cmds.push_back(DrawCmd{ry * tile_h, 0, monster_visuals.size() - 1});
    }

    for (const auto& n : game_state.npcs) {
        SpriteSheetView sheet_view{};
        if (monster_sheet_view) sheet_view = monster_sheet_view(n.sprite_tileset);
        const Sprites* npc_sprites = (sheet_view.ready && sheet_view.sprites) ? sheet_view.sprites : &sprites;
        const Texture2D npc_tex = (sheet_view.ready && sheet_view.texture.id != 0) ? sheet_view.texture : character_tex;
        const bool npc_ready = (sheet_view.ready && sheet_view.sprites) ? true : sprite_ready;

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
                                        key,
                                        n.x,
                                        n.y,
                                        dt,
                                        cfg.monster_slide_tiles_per_sec);
        npc_visuals.push_back(NpcVisual{
            &n, npc_sprites, npc_tex, npc_ready, &anim, rx, ry
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
        auto& attack_t = scene_state.attack_fx_timer_by_key[key];
        auto last_seq_it = scene_state.last_attack_seq_by_key.find(key);
        if (last_seq_it == scene_state.last_attack_seq_by_key.end()) {
            scene_state.last_attack_seq_by_key[key] = p.attack_anim_seq;
            attack_t = 0.0f;
        } else if (last_seq_it->second != p.attack_anim_seq) {
            last_seq_it->second = p.attack_anim_seq;
            attack_t = 0.35f;
        } else {
            attack_t = std::max(0.0f, attack_t - dt);
        }
        auto& outcome_fx = scene_state.combat_outcome_fx_by_key[key];
        auto last_outcome_it = scene_state.last_combat_outcome_seq_by_key.find(key);
        if (last_outcome_it == scene_state.last_combat_outcome_seq_by_key.end()) {
            scene_state.last_combat_outcome_seq_by_key[key] = p.combat_outcome_seq;
            outcome_fx.timer = 0.0f;
            outcome_fx.outcome = 0;
            outcome_fx.value = 0;
        } else if (last_outcome_it->second != p.combat_outcome_seq) {
            last_outcome_it->second = p.combat_outcome_seq;
            outcome_fx.outcome = p.combat_outcome;
            outcome_fx.value = p.combat_value;
            outcome_fx.timer = (p.combat_outcome == 0) ? 0.0f : kCombatFxDurationSec;
        } else {
            outcome_fx.timer = std::max(0.0f, outcome_fx.timer - dt);
        }
        tickAnimation(anim, sprites, (dx != 0 || dy != 0), attack_t > 0.0f, dt);
        prev = {p.x, p.y};
        const auto [rx, ry] = smoothPos(scene_state.render_pos_by_key,
                                        key,
                                        p.x,
                                        p.y,
                                        dt,
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

    // Mouse interactions are evaluated in map-local coordinates so the same
    // picking math works regardless of viewport offset/centering.
    const float map_ox = cfg.map_origin_x;
    const float map_oy = cfg.map_origin_y;
    const float map_vw = cfg.map_view_width > 0.0f ? cfg.map_view_width : (cfg.inventory_visible ? (GetScreenWidth() - cfg.inventory_panel_width) : static_cast<float>(GetScreenWidth()));
    const float map_vh = cfg.map_view_height > 0.0f ? cfg.map_view_height : (GetScreenHeight() - cfg.bottom_panel_height);
    const Vector2 mouse = GetMousePosition();
    const Vector2 mouse_local{mouse.x - map_ox, mouse.y - map_oy};
    const bool has_inventory_drop_rect = cfg.inventory_drop_rect.width > 0.0f && cfg.inventory_drop_rect.height > 0.0f;
    const bool mouse_in_inventory = cfg.inventory_visible &&
                                    has_inventory_drop_rect &&
                                    CheckCollisionPointRec(mouse, cfg.inventory_drop_rect);
    const bool mouse_in_map = mouse.x >= map_ox &&
                              mouse.y >= map_oy &&
                              mouse.x < (map_ox + map_vw) &&
                              mouse.y < (map_oy + map_vh);
    const int hovered_item_id = mouse_in_map ? findTopItemAt(mouse_local) : -1;

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && scene_state.dragging_ground_item_id < 0 && hovered_item_id >= 0) {
        scene_state.dragging_ground_item_id = hovered_item_id;
    }
    // Releasing a dragged ground item either throws it onto map tiles or
    // sends a pickup intent when released over inventory.
    if (scene_state.dragging_ground_item_id >= 0 && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        if (mouse_in_map) {
            const int tx = static_cast<int>(std::floor(mouse_local.x / tile_w));
            const int ty = static_cast<int>(std::floor(mouse_local.y / tile_h));
            out.move_ground_item = MoveGroundItemMsg{scene_state.dragging_ground_item_id, tx, ty};
        } else if (mouse_in_inventory) {
            out.pickup_ground_item = PickupMsg{scene_state.dragging_ground_item_id};
        }
        scene_state.dragging_ground_item_id = -1;
    }
    if (scene_state.dragging_ground_item_id >= 0 || hovered_item_id >= 0) {
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    } else {
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    }

    rlPushMatrix();
    rlTranslatef(map_ox, map_oy, 0.0f);

    // Ground-space attack marker (behind sprites).
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
        if (itv.ready && itv.sprites) {
            const ClipKind kind = itemClipKind(*itv.msg);
            const Frame* fr = itv.sprites->frame(itv.msg->sprite_name, Dir::S, 0, kind);
            if (!fr && kind == ClipKind::Death) fr = itv.sprites->frame(itv.msg->sprite_name, Dir::S, 0, ClipKind::Move);
            if (!fr) fr = itv.sprites->frame(itv.msg->sprite_name, Dir::W, 0, ClipKind::Move);
            if (!fr) fr = itv.sprites->frame(itv.msg->sprite_name, Dir::S, 0, ClipKind::Move);
            if (fr) {
                const Rectangle src = fr->rect();
                Rectangle dst{itv.tile_box.x, itv.tile_box.y, src.width * cfg.map_scale, src.height * cfg.map_scale};
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
                drawActor(*mv.sprites, mv.tex, *mv.anim, mv.rx, mv.ry, tile_w, tile_h, cfg.map_scale, WHITE);
            } else {
                const float x = mv.rx * tile_w + tile_w * 0.5f;
                const float y = mv.ry * tile_h + tile_h * 0.5f;
                DrawCircle(static_cast<int>(x), static_cast<int>(y), 10, RED);
            }
        } else if (cmd.kind == 1) {
            const auto& nv = npc_visuals[cmd.idx];
            if (nv.ready) {
                drawActor(*nv.sprites, nv.tex, *nv.anim, nv.rx, nv.ry, tile_w, tile_h, cfg.map_scale, WHITE);
            } else {
                const float x = nv.rx * tile_w + tile_w * 0.5f;
                const float y = nv.ry * tile_h + tile_h * 0.5f;
                DrawCircle(static_cast<int>(x), static_cast<int>(y), 9, SKYBLUE);
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
                const ClipKind kind = itemClipKind(*drag_item->msg);
                const Frame* fr = drag_item->sprites->frame(drag_item->msg->sprite_name, Dir::S, 0, kind);
                if (!fr && kind == ClipKind::Death) fr = drag_item->sprites->frame(drag_item->msg->sprite_name, Dir::S, 0, ClipKind::Move);
                if (!fr) fr = drag_item->sprites->frame(drag_item->msg->sprite_name, Dir::W, 0, ClipKind::Move);
                if (!fr) fr = drag_item->sprites->frame(drag_item->msg->sprite_name, Dir::S, 0, ClipKind::Move);
                if (fr) {
                    const Rectangle src = fr->rect();
                    Rectangle dst{mouse_local.x - (src.width * cfg.map_scale * 0.5f),
                                  mouse_local.y - (src.height * cfg.map_scale * 0.75f),
                                  src.width * cfg.map_scale,
                                  src.height * cfg.map_scale};
                    DrawTexturePro(drag_item->tex, src, dst, Vector2{0, 0}, 0.0f, Fade(WHITE, 0.85f));
                }
            } else {
                DrawCircleV(mouse_local, 8.0f, Fade(GOLD, 0.8f));
            }
        }
    }

    auto drawCombatOutcomeFx = [&](const std::string& key, float feet_x, float feet_y) {
        auto it = scene_state.combat_outcome_fx_by_key.find(key);
        if (it == scene_state.combat_outcome_fx_by_key.end()) return;
        const SceneState::CombatOutcomeFx& fx = it->second;
        if (!combat_fx_ready || combat_fx_tex.id == 0) return;
        if (fx.timer <= 0.0f || fx.outcome <= 0) return;
        const CombatFxAtlas& atlas = combatFxAtlas();
        const float t = std::max(0.0f, std::min(1.0f, fx.timer / kCombatFxDurationSec));
        const std::vector<int>* frames = nullptr;
        if (fx.outcome == 1) frames = &atlas.hit_frames;
        else if (fx.outcome == 2) frames = &atlas.wiff_frames;
        else if (fx.outcome == 3) frames = &atlas.block_frames;
        if (!frames || frames->empty()) return;
        const float p = 1.0f - t;
        const int idx = std::max(0, std::min(static_cast<int>(frames->size()) - 1,
                                             static_cast<int>(std::floor(p * static_cast<float>(frames->size())))));
        const int tile_id = (*frames)[static_cast<size_t>(idx)];
        const int tx = tile_id % std::max(1, atlas.columns);
        const int ty = tile_id / std::max(1, atlas.columns);
        Rectangle src{
            static_cast<float>(tx * atlas.tile_w),
            static_cast<float>(ty * atlas.tile_h),
            static_cast<float>(atlas.tile_w),
            static_cast<float>(atlas.tile_h)
        };
        const float sz = std::max(tile_w, tile_h) * 1.1f;
        const float px = feet_x - sz * 0.5f;
        const float py = feet_y - tile_h * 1.40f + kCombatFxYOffsetPx;
        Rectangle dst{px, py, sz, sz};
        Color tint = WHITE;
        tint.a = static_cast<unsigned char>(255.0f * kCombatFxOpacity);
        DrawTexturePro(combat_fx_tex, src, dst, Vector2{0, 0}, 0.0f, tint);
        if (fx.outcome == kCombatOutcomeHit && fx.value > 0) {
            const std::string dmg = std::to_string(fx.value);
            const float font_size = std::max(14.0f, 16.0f * uiScreenScale());
            const Vector2 ts = MeasureTextEx(ui_font, dmg.c_str(), font_size, 1.0f);
            const float progress = 1.0f - t;
            const float rise_px = progress * 34.0f;
            Color c{235, 48, 48, static_cast<unsigned char>(255.0f * t)};
            drawUiText(ui_font,
                       dmg,
                       feet_x - ts.x * 0.5f,
                       py - ts.y - 2.0f - rise_px,
                       font_size,
                       c);
        }
    };

    for (const auto& mv : monster_visuals) {
        const auto& m = *mv.msg;
        const float bar_y = mv.click_box.y + (tile_h * 0.5f) - 10.0f;
        drawHealthBar(mv.click_box.x, bar_y, mv.click_box.width, m.hp, m.max_hp);
        drawCombatOutcomeFx("m:" + std::to_string(m.id),
                            mv.rx * tile_w + mv.click_box.width * 0.5f,
                            mv.ry * tile_h + tile_h * 0.5f);
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
        drawCombatOutcomeFx("p:" + p.user, x, y);
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
                        bool speech_ready,
                        Font ui_font,
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

    // Most recent first should appear on top, so draw oldest to newest.
    std::sort(bubbles.begin(), bubbles.end(), [](const Bubble& a, const Bubble& b) {
        return a.seq < b.seq;
    });

    for (const auto& b : bubbles) {
        if (speech_ready && speech_tex.id != 0) {
            drawTalkBubble(speech_tex,
                           ui_font,
                           b.speech_type,
                           b.text,
                           b.head_x,
                           b.head_y,
                           cfg.map_scale,
                           cfg.map_view_width,
                           cfg.speech_text_size,
                           cfg.speech_bubble_alpha);
        } else {
            const float font_size = std::max(10.0f, cfg.speech_text_size * uiScreenScale());
            const Vector2 text_size = MeasureTextEx(ui_font, b.text.c_str(), font_size, 1.0f);
            const float bubble_w = std::max(32.0f, text_size.x + 14.0f);
            const float bubble_h = std::max(18.0f, text_size.y + 10.0f);
            const float bubble_x = b.head_x - bubble_w * 0.5f;
            const float bubble_y = b.head_y - bubble_h - 18.0f;
            const unsigned char a = static_cast<unsigned char>(std::max(0.0f, std::min(1.0f, cfg.speech_bubble_alpha)) * 255.0f);
            DrawRectangleRounded(Rectangle{bubble_x, bubble_y, bubble_w, bubble_h}, 0.2f, 6, Color{250, 250, 250, a});
            DrawRectangleLinesEx(Rectangle{bubble_x, bubble_y, bubble_w, bubble_h}, 1.0f, Color{20, 20, 20, a});
            drawUiText(ui_font, b.text, bubble_x + 7.0f, bubble_y + 5.0f, font_size, BLACK);
        }
    }
}

} // namespace client
