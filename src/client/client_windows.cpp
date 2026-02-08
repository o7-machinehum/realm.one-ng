#include "client_windows.h"

#include "client_support.h"

#include <algorithm>

namespace client {

namespace {

constexpr float kHeaderH = kUiWindowHeaderHeight;
constexpr float kCollapseBtn = 14.0f;

Rectangle collapseRect(const UiWindow& w) {
    return Rectangle{
        w.rect.x + w.rect.width - kCollapseBtn - 8.0f,
        w.rect.y + (kHeaderH - kCollapseBtn) * 0.5f,
        kCollapseBtn,
        kCollapseBtn
    };
}

Rectangle bodyRect(const UiWindow& w) {
    if (w.collapsed) return Rectangle{w.rect.x, w.rect.y + kHeaderH, w.rect.width, 0.0f};
    return Rectangle{w.rect.x, w.rect.y + kHeaderH, w.rect.width, std::max(0.0f, w.rect.height - kHeaderH)};
}

bool inHeaderDragZone(const UiWindow& w, Vector2 m) {
    const Rectangle hr{w.rect.x, w.rect.y, w.rect.width, kHeaderH};
    if (!CheckCollisionPointRec(m, hr)) return false;
    return !CheckCollisionPointRec(m, collapseRect(w));
}

} // namespace

UiWindow& ensureWindow(UiWindowsState& state,
                       const std::string& id,
                       const std::string& title,
                       const Rectangle& default_rect,
                       bool drag_y_only) {
    auto it = state.by_id.find(id);
    if (it == state.by_id.end()) {
        UiWindow w;
        w.id = id;
        w.title = title;
        w.rect = default_rect;
        w.drag_y_only = drag_y_only;
        it = state.by_id.emplace(id, std::move(w)).first;
    } else {
        it->second.title = title;
        it->second.drag_y_only = drag_y_only;
    }
    return it->second;
}

UiWindowFrameResult drawWindowFrame(UiWindowsState& state,
                                    Font font,
                                    UiWindow& window,
                                    float title_size) {
    UiWindowFrameResult out;
    if (!window.visible) return out;

    const Vector2 m = GetMousePosition();
    const Rectangle cbtn = collapseRect(window);
    const Rectangle body = bodyRect(window);
    const float frame_h = window.collapsed ? kHeaderH : window.rect.height;
    const Rectangle frame_rect{window.rect.x, window.rect.y, window.rect.width, frame_h};

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        if (CheckCollisionPointRec(m, cbtn)) {
            window.collapsed = !window.collapsed;
        } else if (inHeaderDragZone(window, m)) {
            state.dragging_id = window.id;
            state.drag_offset_x = m.x - window.rect.x;
            state.drag_offset_y = m.y - window.rect.y;
            state.drag_started_on_click = true;
        }
    }

    if (!state.dragging_id.empty() && state.dragging_id == window.id) {
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            state.dragging_id.clear();
            state.drag_started_on_click = false;
        } else {
            if (window.drag_y_only) {
                window.rect.y = m.y - state.drag_offset_y;
            } else {
                window.rect.x = m.x - state.drag_offset_x;
                window.rect.y = m.y - state.drag_offset_y;
            }
            window.rect.y = std::max(0.0f, std::min(window.rect.y, static_cast<float>(GetScreenHeight()) - 24.0f));
            window.rect.x = std::max(0.0f, std::min(window.rect.x, static_cast<float>(GetScreenWidth()) - 30.0f));
        }
    }

    DrawRectangleRec(frame_rect, Color{24, 28, 32, 230});
    DrawRectangle(static_cast<int>(window.rect.x), static_cast<int>(window.rect.y),
                  static_cast<int>(window.rect.width), static_cast<int>(kHeaderH),
                  Color{43, 54, 63, 255});
    DrawRectangleLinesEx(frame_rect, 1.0f, Color{96, 109, 124, 255});

    drawUiText(font, window.title, window.rect.x + 8.0f, window.rect.y + 6.0f, title_size, RAYWHITE);

    DrawRectangleRec(cbtn, Color{55, 63, 70, 255});
    DrawRectangleLinesEx(cbtn, 1.0f, Color{120, 126, 135, 255});
    drawUiText(font, window.collapsed ? "+" : "-", cbtn.x + 4.0f, cbtn.y - 1.0f, 16.0f, RAYWHITE);

    out.open = !window.collapsed;
    out.header_rect = Rectangle{window.rect.x, window.rect.y, window.rect.width, kHeaderH};
    out.body_rect = body;
    return out;
}

float windowFrameHeight(const UiWindow& window) {
    return window.collapsed ? kUiWindowHeaderHeight : window.rect.height;
}

} // namespace client
