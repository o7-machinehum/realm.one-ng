#include "client_ui_primitives.h"

#include "raylib.h"

#include <algorithm>
#include <cctype>

namespace client {

// Draws a generic bordered panel background.
void drawUiPanel(const Rectangle& rect, Color fill, Color border, float border_thickness) {
    DrawRectangleRec(rect, fill);
    DrawRectangleLinesEx(rect, border_thickness, border);
}

// Draws a generic button and returns true when clicked.
bool drawUiButton(const Rectangle& rect, Font font, const char* label, float font_size) {
    const Vector2 m = GetMousePosition();
    const bool hot = CheckCollisionPointRec(m, rect);
    drawUiPanel(rect,
                hot ? Color{70, 88, 118, 255} : Color{48, 58, 78, 255},
                Color{110, 126, 156, 255},
                1.0f);
    const Vector2 sz = MeasureTextEx(font, label, font_size, 1.0f);
    DrawTextEx(font,
               label,
               Vector2{rect.x + (rect.width - sz.x) * 0.5f, rect.y + (rect.height - sz.y) * 0.5f},
               font_size,
               1.0f,
               RAYWHITE);
    return hot && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

// Appends username-safe typed characters into a text buffer.
void appendUsernameInputChars(std::string& value, int max_len) {
    while (int key = GetCharPressed()) {
        const bool ok = std::isalnum(key) || key == '_' || key == '-';
        if (ok && static_cast<int>(value.size()) < max_len) {
            value.push_back(static_cast<char>(key));
        }
    }
}

// Draws a single-line text input with shared caret behavior.
void drawUiTextInputLine(Font font,
                         const std::string& text,
                         float x,
                         float y,
                         float size,
                         const std::string& prefix,
                         bool active,
                         Color active_color,
                         Color inactive_color) {
    const bool blink_on = (static_cast<int>(GetTime() * 2.0) % 2) == 0;
    const std::string caret = (active && blink_on) ? "_" : "";
    const std::string prompt = prefix + text + caret;
    DrawTextEx(font, prompt.c_str(), Vector2{x, y}, size, 1.0f, active ? active_color : inactive_color);
}

} // namespace client
