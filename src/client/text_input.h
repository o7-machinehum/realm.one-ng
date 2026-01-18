#pragma once

#include <raylib.h>

#include <deque>
#include <optional>
#include <string>

class TextInput {
public:
    struct Config {
        int maxLen = 120;
        int logLinesVisible = 8;   // how many to draw
        int logMaxLines = 200;     // how many to keep
        int padding = 10;
        int boxHeight = 44;
        int fontSize = 18;
        float cursorBlinkHz = 2.0f;

        std::string placeholder = "Type a command and press Enter...";
    };

    TextInput() = default;
    explicit TextInput(Config cfg);

    void setConfig(Config cfg) { cfg_ = std::move(cfg); }
    const Config& config() const { return cfg_; }

    // Console mode
    bool isOpen() const { return open_; }
    void open() { open_ = true; }
    void close() { open_ = false; input_.clear(); }

    // Call once per frame
    void update(float dt);

    // Draw at bottom of screen. Pass screen width/height (from GetScreenWidth/Height).
    void draw(int screenW, int screenH) const;

    // Returns a submitted line when Enter is pressed while open. Consumes it.
    std::optional<std::string> pollSubmitted();

    // Current line being edited
    const std::string& input() const { return input_; }

    // Log / scrollback
    void pushLog(std::string line);
    void clearLog();
    const std::deque<std::string>& log() const { return log_; }

private:
    void handleTextInput();
    void handleEditingKeys();
    void handleScrollKeys();
    void updateCursorBlink(float dt);

    Config cfg_{};

    bool open_ = false;

    std::string input_;
    std::deque<std::string> log_;
    std::optional<std::string> submitted_;

    // Scrollback
    int scrollOffset_ = 0; // 0 = bottom (latest), positive = scrolled up

    // Cursor blink
    float blinkT_ = 0.0f;
    bool cursorOn_ = true;
};

