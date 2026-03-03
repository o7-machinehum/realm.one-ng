// Speech bubble rendering for talk / think / yell overlays.
#pragma once

#include <raylib.h>

#include <string>
#include <vector>

namespace client {

// Wraps text into lines that fit within max_text_width pixels.
[[nodiscard]] std::vector<std::string> wrapSpeechText(Font font,
                                                       const std::string& text,
                                                       float font_size,
                                                       float max_text_width);

// Draws a nine-patch speech bubble with a tail pointing at (head_x, head_y).
void drawTalkBubble(Texture2D speech_tex,
                    Font ui_font,
                    Font ui_bold_font,
                    const std::string& speech_type,
                    const std::string& text,
                    float head_x,
                    float head_y,
                    float map_scale,
                    float map_view_width,
                    float speech_text_size,
                    float bubble_alpha);

} // namespace client
