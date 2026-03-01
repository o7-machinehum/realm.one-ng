#include "speech_bubble.h"

#include "client_support.h"
#include "string_util.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace client {
namespace {

constexpr int kSpeechCols = 9;
constexpr float kSpeechTilePx = 16.0f;
constexpr float kSpeechTextPadX = 8.0f;
constexpr float kSpeechTextPadY = 6.0f;

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
    const std::string type = toLowerAscii(raw_type);
    if (type == "think") {
        switch (pos) {
            case 'a': return 15; case 'b': return 17; case 'c': return 24;
            case 'd': return  6; case 'e': return  7; case 'f': return  8;
            case 'g': return 16; case 'h': return 25; case 'i': return 40;
            case 'j': return 49; case 't': return 26; default:  return 16;
        }
    }
    if (type == "yell") {
        switch (pos) {
            case 'a': return 36; case 'b': return 38; case 'c': return 45;
            case 'd': return 27; case 'e': return 28; case 'f': return 29;
            case 'g': return 37; case 'h': return 46; case 'i': return 22;
            case 'j': return 31; case 't': return 47; default:  return 37;
        }
    }
    // Default: talk
    switch (pos) {
        case 'a': return  9; case 'b': return 11; case 'c': return 18;
        case 'd': return  0; case 'e': return  1; case 'f': return  2;
        case 'g': return 10; case 'h': return 19; case 'i': return  4;
        case 'j': return 13; case 't': return 20; default:  return 10;
    }
}

void drawSpeechTile(Texture2D tex, int tile_id, float x, float y, float size_px, float bubble_alpha) {
    const Rectangle src = speechSrcRect(tile_id);
    const Rectangle dst{x, y, size_px, size_px};
    DrawTexturePro(tex, src, dst, Vector2{0, 0}, 0.0f, Fade(WHITE, bubble_alpha));
}

} // namespace

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
    const float text_block_h = line_h * static_cast<float>(lines.size()) +
                               std::max(0.0f, static_cast<float>(lines.size() - 1)) * 1.0f;

    const float inner_w_px = max_line_w + kSpeechTextPadX * 2.0f;
    const float inner_h_px = text_block_h + kSpeechTextPadY * 2.0f;

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
        : (compact_one_liner ? 1 : std::max(1, static_cast<int>(std::ceil(inner_h_px / tile_px))));
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

    // Single tail tile
    const float tail_x = bubble_x + center_col * tile_px;
    const float tail_y = bottom_y + tile_px * 0.88f;
    drawSpeechTile(speech_tex, speechTileId(speech_type, 'j'), tail_x, tail_y, tile_px, bubble_alpha);

    // Draw text centered in bubble
    const float text_top = bubble_y + (bubble_h - text_block_h) * 0.5f - tile_px * 0.15f;
    for (size_t i = 0; i < lines.size(); ++i) {
        const float lw = MeasureTextEx(ui_font, lines[i].c_str(), font_size, 1.0f).x;
        const float text_x = bubble_x + (bubble_w - lw) * 0.5f;
        const float text_y = text_top + static_cast<float>(i) * (line_h + 1.0f);
        drawUiText(ui_font, lines[i], text_x, text_y, font_size, BLACK);
    }
}

} // namespace client
