#pragma once

#include "raylib.h"

#include <string>
#include <unordered_map>

namespace client {

constexpr float kUiWindowHeaderHeight = 28.0f;

struct UiWindow {
    std::string id;
    std::string title;
    Rectangle rect{};
    bool collapsed = false;
    bool visible = true;
    bool drag_y_only = true;
};

struct UiWindowsState {
    std::unordered_map<std::string, UiWindow> by_id;
    std::string dragging_id;
    float drag_offset_x = 0.0f;
    float drag_offset_y = 0.0f;
    bool drag_started_on_click = false;
};

struct UiWindowFrameResult {
    bool open = false;
    Rectangle header_rect{};
    Rectangle body_rect{};
};

UiWindow& ensureWindow(UiWindowsState& state,
                       const std::string& id,
                       const std::string& title,
                       const Rectangle& default_rect,
                       bool drag_y_only = true);

UiWindowFrameResult drawWindowFrame(UiWindowsState& state,
                                    Font font,
                                    UiWindow& window,
                                    float title_size = 15.0f);

float windowFrameHeight(const UiWindow& window);

} // namespace client
