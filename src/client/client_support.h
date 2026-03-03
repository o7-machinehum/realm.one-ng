#pragma once

#include "msg.h"
#include "sprites.h"
#include "raylib.h"

#include <deque>
#include <string>

namespace client {

struct AnimationComponent {
    std::string sprite_name = "player_1";
    Dir dir = Dir::S;
    int frame_index = 0;
    float timer = 0.0f;
    float frame_time = 0.14f;
    float moving_timer = 0.0f;
    bool action_playing = false;
};

struct DragState {
    bool active = false;
    int from_index = -1;
    std::string item;
};

void pushBounded(std::deque<std::string>& logs, std::string line);

bool tryParseLogin(const std::string& input, LoginMsg& out);
bool tryParsePickup(const std::string& input, PickupMsg& out);
bool tryParseDrop(const std::string& input, DropMsg& out);
bool tryParseMove(const std::string& input, MoveMsg& out);
bool tryParseAttack(const std::string& input, AttackMsg& out);

Dir dirFromDelta(int dx, int dy, Dir fallback);
void tickAnimation(AnimationComponent& anim, const Sprites& sprites, bool movement_pulse, bool action_active, float dt);

void drawActor(const Sprites& sprites,
               Texture2D tex,
               const AnimationComponent& anim,
               float tile_x,
               float tile_y,
               float tile_w,
               float tile_h,
               float scale,
               Color tint);

Font loadUIFont();
Font loadUIBoldFont();
float uiScreenScale();
void drawUiText(Font font, const std::string& text, float x, float y, float size, Color color);
void drawHealthBar(float x, float y, float w, int hp, int max_hp);

// Draw a single tile from a tilesheet atlas. Computes the source rect from
// tile_id using the grid layout (columns, tile_w, tile_h) and draws into dst.
void drawAtlasTile(Texture2D tex, int tile_id, int columns,
                   int tile_w, int tile_h, Rectangle dst, Color tint);

// Draw text that floats upward and fades out. Centered horizontally at center_x,
// starting at base_y and rising by rise_px * progress. Alpha fades to zero as
// progress goes from 0 (just appeared) to 1 (fully faded).
void drawFloatingText(Font font, const std::string& text,
                      float center_x, float base_y,
                      Color color, float progress, float rise_px);

int slotAtPoint(const Rectangle& panel, const Vector2& p, int cols, float slot_size, float gap, int max_slots);

} // namespace client
