
#include "text_input.h"

#include <algorithm>

TextInput::TextInput(Config cfg) : cfg_(std::move(cfg)) {}

void TextInput::pushLog(std::string line) {
    log_.push_back(std::move(line));
    while ((int)log_.size() > cfg_.logMaxLines) log_.pop_front();
    // When new log arrives, snap to bottom.
    scrollOffset_ = 0;
}

void TextInput::clearLog() {
    log_.clear();
    scrollOffset_ = 0;
}

std::optional<std::string> TextInput::pollSubmitted() {
    auto out = submitted_;
    submitted_.reset();
    return out;
}

static std::string trimStr(std::string s) {
    auto is_space = [](unsigned char c){ return std::isspace(c); };
    while (!s.empty() && is_space((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && is_space((unsigned char)s.back())) s.pop_back();
    return s;
}

void TextInput::handleTextInput() {
    for (int c = GetCharPressed(); c > 0; c = GetCharPressed()) {
        if ((int)input_.size() >= cfg_.maxLen) continue;

        if (c >= 32 && c <= 126) {
            input_.push_back((char)c);
        }
    }
}

void TextInput::handleEditingKeys() {
    if (IsKeyPressed(KEY_BACKSPACE)) {
        if (!input_.empty()) input_.pop_back();
    }

    // Ctrl+U clears line
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_U)) {
        input_.clear();
    }

    // Esc closes console (and clears current line)
    if (IsKeyPressed(KEY_ESCAPE)) {
        close();
        return;
    }

    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        std::string s = trimStr(input_);
        if (!s.empty()) {
            submitted_ = s;
            pushLog("> " + s);
        }
        input_.clear();

        // Close after submitting (so WASD is clean again)
        close();
    }
}

void TextInput::handleScrollKeys() {
    // Mouse wheel: positive is typically up in raylib
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        scrollOffset_ += (int)(wheel * 3); // scroll speed
        if (scrollOffset_ < 0) scrollOffset_ = 0;
        int maxOffset = std::max(0, (int)log_.size() - cfg_.logLinesVisible);
        if (scrollOffset_ > maxOffset) scrollOffset_ = maxOffset;
    }

    // PageUp/PageDown
    if (IsKeyPressed(KEY_PAGE_UP)) scrollOffset_ += cfg_.logLinesVisible;
    if (IsKeyPressed(KEY_PAGE_DOWN)) scrollOffset_ -= cfg_.logLinesVisible;

    if (scrollOffset_ < 0) scrollOffset_ = 0;
    int maxOffset = std::max(0, (int)log_.size() - cfg_.logLinesVisible);
    if (scrollOffset_ > maxOffset) scrollOffset_ = maxOffset;
}

void TextInput::updateCursorBlink(float dt) {
    blinkT_ += dt;
    const float hz = (cfg_.cursorBlinkHz <= 0.01f) ? 0.01f : cfg_.cursorBlinkHz;
    const float period = 1.0f / hz;
    if (blinkT_ >= period) {
        blinkT_ = 0.0f;
        cursorOn_ = !cursorOn_;
    }
}

void TextInput::update(float dt) {
    // If closed: pressing Enter opens console (starts typing mode)
    if (!open_) {
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            open();
        }
        // do NOT consume typed chars while closed
        updateCursorBlink(dt);
        return;
    }

    handleScrollKeys();
    handleTextInput();
    handleEditingKeys();
    updateCursorBlink(dt);
}

void TextInput::draw(int screenW, int screenH) const {
    const int pad = cfg_.padding;
    const int boxH = cfg_.boxHeight;
    const int font = cfg_.fontSize;

    // Log area height
    const int logLineH = font + 6;
    const int logH = logLineH * cfg_.logLinesVisible;

    // Input box at bottom
    Rectangle box{
        (float)pad,
        (float)(screenH - boxH - pad),
        (float)(screenW - pad * 2),
        (float)boxH
    };

    // Log background just above the box
    Rectangle logBg{
        box.x,
        box.y - (float)logH - 6,
        box.width,
        (float)logH + 6
    };

    DrawRectangle((int)logBg.x, (int)logBg.y, (int)logBg.width, (int)logBg.height, Fade(LIGHTGRAY, 0.25f));

    // Which slice of log to show
    const int total = (int)log_.size();
    const int visible = std::min(cfg_.logLinesVisible, total);
    const int start = std::max(0, total - visible - scrollOffset_);
    const int end = start + visible;

    int y = (int)logBg.y + 6;
    for (int i = start; i < end; i++) {
        DrawText(log_[i].c_str(), (int)box.x + 6, y, font, DARKGRAY);
        y += logLineH;
    }

    // Input box (dim if closed)
    Color fill = open_ ? Fade(WHITE, 0.90f) : Fade(WHITE, 0.60f);
    DrawRectangleRounded(box, 0.15f, 8, fill);
    DrawRectangleLinesEx(box, 2.0f, Fade(DARKGRAY, open_ ? 0.35f : 0.20f));

    // Text inside box
    std::string shown;
    Color textColor;

    if (!open_) {
        shown = "Press Enter to type...";
        textColor = Fade(DARKGRAY, 0.6f);
    } else if (input_.empty()) {
        shown = cfg_.placeholder;
        textColor = Fade(DARKGRAY, 0.5f);
    } else {
        shown = input_;
        textColor = DARKGRAY;
    }

    if (open_ && cursorOn_) shown.push_back('|');

    DrawText(shown.c_str(), (int)box.x + 10, (int)box.y + (boxH - font) / 2, font, textColor);
}

