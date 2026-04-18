#pragma once

#include <string>

struct Font;
struct Rectangle;
struct Color;
struct Vector2;

namespace client {

// Draws a generic bordered panel background.
void drawUiPanel(const Rectangle& rect, Color fill, Color border, float border_thickness = 1.0f);

// Draws a generic button and returns true when clicked.
bool drawUiButton(const Rectangle& rect,
                  Font font,
                  const char* label,
                  float font_size = 24.0f);

// Appends username-safe typed characters into a text buffer.
void appendUsernameInputChars(std::string& value, int max_len);

// Draws a single-line text input with shared caret behavior.
void drawUiTextInputLine(Font font,
                         const std::string& text,
                         float x,
                         float y,
                         float size,
                         const std::string& prefix,
                         bool active,
                         Color active_color,
                         Color inactive_color);

} // namespace client
