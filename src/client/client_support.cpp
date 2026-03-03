#include "client_support.h"

#include <algorithm>
#include <sstream>

namespace client {

void pushBounded(std::deque<std::string>& logs, std::string line) {
    logs.push_back(std::move(line));
    while (logs.size() > 8) logs.pop_front();
}

bool tryParseLogin(const std::string& input, LoginMsg& out) {
    std::istringstream iss(input);
    std::string cmd;
    iss >> cmd >> out.user;
    return cmd == "/login" && !out.user.empty();
}

bool tryParsePickup(const std::string& input, PickupMsg& out) {
    std::istringstream iss(input);
    std::string cmd;
    iss >> cmd;
    if (cmd != "/pickup") return false;
    if (!(iss >> out.item_id)) out.item_id = -1;
    return true;
}

bool tryParseDrop(const std::string& input, DropMsg& out) {
    std::istringstream iss(input);
    std::string cmd;
    iss >> cmd >> out.inventory_index;
    out.to_x = -1;
    out.to_y = -1;
    return cmd == "/drop";
}

bool tryParseMove(const std::string& input, MoveMsg& out) {
    std::istringstream iss(input);
    std::string cmd, dir;
    iss >> cmd >> dir;
    if (cmd != "/move") return false;
    if (dir == "n" || dir == "north") out = MoveMsg{0, -1};
    else if (dir == "s" || dir == "south") out = MoveMsg{0, 1};
    else if (dir == "e" || dir == "east") out = MoveMsg{1, 0};
    else if (dir == "w" || dir == "west") out = MoveMsg{-1, 0};
    else return false;
    return true;
}

bool tryParseAttack(const std::string& input, AttackMsg& out) {
    std::istringstream iss(input);
    std::string cmd;
    iss >> cmd;
    if (cmd != "/attack") return false;
    if (!(iss >> out.target_monster_id)) out.target_monster_id = -1;
    return true;
}

Dir dirFromDelta(int dx, int dy, Dir fallback) {
    if (dx > 0) return Dir::E;
    if (dx < 0) return Dir::W;
    if (dy > 0) return Dir::S;
    if (dy < 0) return Dir::N;
    return fallback;
}

void tickAnimation(AnimationComponent& anim, const Sprites& sprites, bool movement_pulse, bool action_active, float dt) {
    const int action_count = sprites.frame_count(anim.sprite_name, anim.dir, ClipKind::Action);
    anim.action_playing = action_active && action_count > 0;
    const int count = anim.action_playing
                    ? action_count
                    : sprites.frame_count(anim.sprite_name, anim.dir, ClipKind::Move);
    if (count <= 0) {
        anim.frame_index = 0;
        anim.timer = 0.0f;
        anim.moving_timer = 0.0f;
        anim.action_playing = false;
        return;
    }

    if (anim.action_playing) {
        anim.timer += dt;
        const float action_frame_time = 0.10f;
        while (anim.timer >= action_frame_time) {
            anim.timer -= action_frame_time;
            anim.frame_index = (anim.frame_index + 1) % count;
        }
        return;
    }

    if (movement_pulse) {
        anim.moving_timer = 0.22f;
    } else {
        anim.moving_timer = std::max(0.0f, anim.moving_timer - dt);
    }

    const bool moving = anim.moving_timer > 0.0f;
    if (!moving) {
        anim.frame_index = 0;
        anim.timer = 0.0f;
        return;
    }

    anim.timer += dt;
    while (anim.timer >= anim.frame_time) {
        anim.timer -= anim.frame_time;
        anim.frame_index = (anim.frame_index + 1) % count;
    }
}

void drawActor(const Sprites& sprites,
               Texture2D tex,
               const AnimationComponent& anim,
               float tile_x,
               float tile_y,
               float tile_w,
               float tile_h,
               float scale,
               Color tint) {
    const ClipKind kind = anim.action_playing ? ClipKind::Action : ClipKind::Move;
    const int count = sprites.frame_count(anim.sprite_name, anim.dir, kind);
    if (count <= 0) return;

    const int idx = std::max(0, std::min(anim.frame_index, count - 1));
    const Frame* fr = sprites.frame(anim.sprite_name, anim.dir, idx, kind);
    if (!fr) return;

    const Rectangle src = fr->rect();
    const float world_x = tile_x * tile_w;
    const float world_y = tile_y * tile_h;

    Rectangle dst{};
    dst.width = src.width * scale;
    dst.height = src.height * scale;
    dst.x = world_x + (tile_w - dst.width) * 0.5f;
    dst.y = world_y + tile_h - dst.height;

    DrawTexturePro(tex, src, dst, Vector2{0, 0}, 0.0f, tint);
}

Font loadUIFont() {
    const char* path = "game/assets/fonts/AtkinsonHyperlegible-Regular.ttf";
    return LoadFontEx(path, 40, nullptr, 0);
}

float uiScreenScale() {
    constexpr float kUiBaseW = 1200.0f;
    constexpr float kUiBaseH = 760.0f;
    const float sx = static_cast<float>(GetScreenWidth()) / kUiBaseW;
    const float sy = static_cast<float>(GetScreenHeight()) / kUiBaseH;
    return std::max(0.85f, std::min(2.2f, std::min(sx, sy)));
}

void drawUiText(Font font, const std::string& text, float x, float y, float size, Color color) {
    DrawTextEx(font, text.c_str(), Vector2{x, y}, size, 1.0f, color);
}

void drawHealthBar(float x, float y, float w, int hp, int max_hp) {
    DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), 5, Fade(BLACK, 0.8f));
    if (max_hp <= 0) return;
    const float pct = std::max(0.0f, std::min(1.0f, static_cast<float>(hp) / static_cast<float>(max_hp)));
    const Color c = (pct > 0.6f) ? LIME : (pct > 0.3f ? ORANGE : RED);
    DrawRectangle(static_cast<int>(x + 1), static_cast<int>(y + 1), static_cast<int>((w - 2) * pct), 3, c);
}

void drawAtlasTile(Texture2D tex, int tile_id, int columns,
                   int tile_w, int tile_h, Rectangle dst, Color tint) {
    const int tx = tile_id % std::max(1, columns);
    const int ty = tile_id / std::max(1, columns);
    Rectangle src{
        static_cast<float>(tx * tile_w),
        static_cast<float>(ty * tile_h),
        static_cast<float>(tile_w),
        static_cast<float>(tile_h)
    };
    DrawTexturePro(tex, src, dst, Vector2{0, 0}, 0.0f, tint);
}

void drawFloatingText(Font font, const std::string& text,
                      float center_x, float base_y,
                      Color color, float progress, float rise_px) {
    const float font_size = std::max(14.0f, 16.0f * uiScreenScale());
    const Vector2 size = MeasureTextEx(font, text.c_str(), font_size, 1.0f);
    const float x = center_x - size.x * 0.5f;
    const float y = base_y - size.y - progress * rise_px;
    color.a = static_cast<unsigned char>(static_cast<float>(color.a) * (1.0f - progress));
    DrawTextEx(font, text.c_str(), Vector2{x, y}, font_size, 1.0f, color);
}

int slotAtPoint(const Rectangle& panel, const Vector2& p, int cols, float slot_size, float gap, int max_slots) {
    if (!CheckCollisionPointRec(p, panel)) return -1;
    const float start_x = panel.x + 16.0f;
    const float start_y = panel.y + 50.0f;
    for (int i = 0; i < max_slots; ++i) {
        const int c = i % cols;
        const int r = i / cols;
        Rectangle slot{start_x + c * (slot_size + gap), start_y + r * (slot_size + gap), slot_size, slot_size};
        if (CheckCollisionPointRec(p, slot)) return i;
    }
    return -1;
}

} // namespace client
