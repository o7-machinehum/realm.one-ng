#pragma once

#include <string>

struct Font;

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
                    unsigned char a = 255);

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
                          bool active);

} // namespace client
