#include "client_game_ui.h"

#include "client_ui_primitives.h"
#include "raylib.h"

#include <algorithm>

namespace client {

// Draws a compact labeled progress bar for vitals/skills panels.
void drawLabeledBar(Font font,
                    const std::string& label,
                    float x,
                    float y,
                    float w,
                    float h,
                    int value,
                    int max_value,
                    unsigned char r,
                    unsigned char g,
                    unsigned char b,
                    unsigned char a) {
    DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), Color{30, 32, 36, 230});
    DrawRectangleLines(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), Color{90, 100, 110, 255});
    const float pct = (max_value > 0) ? std::max(0.0f, std::min(1.0f, static_cast<float>(value) / static_cast<float>(max_value))) : 0.0f;
    const float fill_w = std::max(0.0f, (w - 2.0f) * pct);
    DrawRectangle(static_cast<int>(x + 1), static_cast<int>(y + 1), static_cast<int>(fill_w), static_cast<int>(h - 2.0f), Color{r, g, b, a});
    const std::string text = label + " " + std::to_string(value) + "/" + std::to_string(max_value);
    DrawTextEx(font, text.c_str(), Vector2{x + 8.0f, y + 3.0f}, 14.0f, 1.0f, RAYWHITE);
}

// Draws the chat input overlay at the bottom of the play viewport.
void drawChatInputOverlay(Font ui_font,
                          int play_x,
                          int play_y,
                          int play_w,
                          int play_h,
                          float overlay_margin,
                          float overlay_height,
                          float text_offset_x,
                          float text_offset_y,
                          float font_size,
                          const std::string& input,
                          bool active,
                          float alpha) {
    const auto a = static_cast<unsigned char>(255.0f * alpha);
    const float input_y = static_cast<float>(play_y + play_h) - overlay_height - overlay_margin;
    const Rectangle chat_input_rect{
        static_cast<float>(play_x) + overlay_margin,
        input_y,
        static_cast<float>(play_w) - (overlay_margin * 2.0f),
        overlay_height
    };
    drawUiPanel(chat_input_rect,
                Color{0, 0, 0, static_cast<unsigned char>(150.0f * alpha)},
                Color{90, 90, 90, a},
                1.0f);
    if (active) SetMouseCursor(MOUSE_CURSOR_IBEAM);
    const std::string prefix = active ? "> " : "[Enter] Chat: ";
    drawUiTextInputLine(ui_font,
                        input,
                        static_cast<float>(play_x) + overlay_margin + text_offset_x,
                        input_y + text_offset_y,
                        font_size,
                        prefix,
                        active,
                        Color{YELLOW.r, YELLOW.g, YELLOW.b, a},
                        Color{LIGHTGRAY.r, LIGHTGRAY.g, LIGHTGRAY.b, a});
}

} // namespace client
