#include "auth_screen.h"

#include "raylib.h"

#include "auth_crypto.h"
#include "local_keys.h"
#include "net_msgs.h"

#include <algorithm>
#include <cstdio>

namespace authui {

namespace {

constexpr int   kFontSize          = 20;
constexpr int   kPadding           = 20;
constexpr int   kLineSpacing       = 28;
constexpr int   kMaxNameLen        = 24;
constexpr Color kBackground        = {18, 22, 30, 255};
constexpr Color kTitleColor        = RAYWHITE;
constexpr Color kHintColor         = LIGHTGRAY;
constexpr Color kSelectedColor     = YELLOW;
constexpr Color kErrorColor        = {240, 100, 100, 255};

enum class Mode {
    ChooseAccount,
    CreatingAccount,
    Authenticating,
    Done,
};

bool isValidNameChar(int key) {
    return (key >= 'a' && key <= 'z') ||
           (key >= 'A' && key <= 'Z') ||
           (key >= '0' && key <= '9') ||
           key == '_' || key == '-';
}

void readTextInput(std::string& s) {
    while (int key = GetCharPressed()) {
        if (isValidNameChar(key) && static_cast<int>(s.size()) < kMaxNameLen) {
            s.push_back(static_cast<char>(key));
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !s.empty()) s.pop_back();
}

bool sendLogin(netc::NetClient& net,
               const localkey::StoredKey& key,
               bool create_account,
               std::string& err) {
    const std::string payload = authx::makeAuthPayload(key.username, key.public_key_hex, create_account);
    const auto signature = authx::signEd25519(key.private_key_hex, payload);
    if (!signature) { err = "failed to sign auth payload"; return false; }

    net::LoginPayload login{
        key.username, key.public_key_hex, *signature, create_account
    };
    net.sendLogin(login);
    return true;
}

bool createAndSubmit(netc::NetClient& net, const std::string& username, std::string& err) {
    const auto pair = authx::generateEd25519KeyPair();
    if (!pair) { err = "failed to generate keypair"; return false; }
    localkey::StoredKey key{username, pair->public_key, pair->private_key};
    if (!localkey::save(key, &err)) return false;
    return sendLogin(net, key, true, err);
}

bool loginExisting(netc::NetClient& net, const std::string& username, std::string& err) {
    const auto key = localkey::load(username, &err);
    if (!key) return false;
    return sendLogin(net, *key, false, err);
}

void drawAccountList(const std::vector<std::string>& users,
                     int selection_index,
                     int top_y) {
    int y = top_y;
    if (users.empty()) {
        DrawText("(no local accounts yet)", kPadding, y, kFontSize, kHintColor);
        y += kLineSpacing;
    }
    for (size_t i = 0; i < users.size(); ++i) {
        const bool selected = (static_cast<int>(i) == selection_index);
        const Color c = selected ? kSelectedColor : kTitleColor;
        DrawText(TextFormat(" %s%s", selected ? "> " : "  ", users[i].c_str()),
                 kPadding, y, kFontSize, c);
        y += kLineSpacing;
    }
    const bool create_selected = (selection_index == static_cast<int>(users.size()));
    DrawText(create_selected ? " > [+] Create new account" : "   [+] Create new account",
             kPadding, y, kFontSize, create_selected ? kSelectedColor : kTitleColor);
}

void drawHint(int y, const char* text) {
    DrawText(text, kPadding, y, kFontSize, kHintColor);
}

void drawError(int y, const std::string& msg) {
    if (msg.empty()) return;
    DrawText(msg.c_str(), kPadding, y, kFontSize, kErrorColor);
}

} // namespace

std::optional<AuthResult> runAuthScreen(netc::NetClient& net) {
    auto users = localkey::listUsernames();
    int selection_index = 0;          // index into [users..., create_option]
    Mode mode = Mode::ChooseAccount;
    std::string create_buffer;
    std::string error_msg;
    std::string pending_username;

    while (!WindowShouldClose()) {
        net.pump(0);
        while (auto msg = net.pop()) {
            if (auto* res = std::get_if<net::LoginResultEvent>(&*msg)) {
                if (res->ok) {
                    return AuthResult{pending_username};
                }
                error_msg = res->message;
                mode = Mode::ChooseAccount;
            }
        }

        switch (mode) {
        case Mode::ChooseAccount: {
            const int total_options = static_cast<int>(users.size()) + 1; // +create
            if (IsKeyPressed(KEY_UP))   selection_index = std::max(0, selection_index - 1);
            if (IsKeyPressed(KEY_DOWN)) selection_index = std::min(total_options - 1, selection_index + 1);
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                if (selection_index < static_cast<int>(users.size())) {
                    pending_username = users[selection_index];
                    error_msg.clear();
                    if (loginExisting(net, pending_username, error_msg)) {
                        mode = Mode::Authenticating;
                    }
                } else {
                    create_buffer.clear();
                    error_msg.clear();
                    mode = Mode::CreatingAccount;
                }
            }
            break;
        }
        case Mode::CreatingAccount: {
            readTextInput(create_buffer);
            if (IsKeyPressed(KEY_ESCAPE)) mode = Mode::ChooseAccount;
            if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && !create_buffer.empty()) {
                pending_username = create_buffer;
                error_msg.clear();
                if (createAndSubmit(net, pending_username, error_msg)) {
                    mode = Mode::Authenticating;
                }
            }
            break;
        }
        case Mode::Authenticating:
        case Mode::Done:
            break;
        }

        BeginDrawing();
        ClearBackground(kBackground);
        DrawText("Realm One — log in", kPadding, kPadding, kFontSize + 6, kTitleColor);

        const int list_top = kPadding + kLineSpacing + 16;
        switch (mode) {
        case Mode::ChooseAccount:
            drawAccountList(users, selection_index, list_top);
            drawHint(list_top + (static_cast<int>(users.size()) + 2) * kLineSpacing,
                     "Up/Down to select, Enter to confirm");
            drawError(list_top + (static_cast<int>(users.size()) + 4) * kLineSpacing, error_msg);
            break;
        case Mode::CreatingAccount:
            DrawText("New account name:", kPadding, list_top, kFontSize, kTitleColor);
            DrawText(TextFormat("> %s_", create_buffer.c_str()),
                     kPadding, list_top + kLineSpacing, kFontSize, kSelectedColor);
            drawHint(list_top + kLineSpacing * 3,
                     "Letters/digits/_- only.  Enter to create.  Esc to cancel.");
            drawError(list_top + kLineSpacing * 5, error_msg);
            break;
        case Mode::Authenticating:
            DrawText("Authenticating...", kPadding, list_top, kFontSize, kTitleColor);
            break;
        case Mode::Done:
            break;
        }
        EndDrawing();
    }

    return std::nullopt;
}

} // namespace authui
