#pragma once

#include "msg.h"
#include "raylib.h"

#include <deque>
#include <string>
#include <vector>

struct Font;
class Mailbox;

namespace client {

// UI/runtime state for pre-game authentication.
struct AuthUiState {
    bool auth_done = false;
    bool show_create_modal = false;
    bool show_help_modal = false;
    bool auth_waiting = false;
    int auth_focus = 1; // 1=login name, 2=create name
    std::string login_name;
    std::string create_name;
    std::string auth_message;
    std::vector<std::string> local_users;
    Texture2D login_bg_tex{};
};

// Initializes auth UI state from local key files.
void initAuthUi(AuthUiState& state);
void shutdownAuthUi(AuthUiState& state);

// Applies a server login result and updates auth UI state/logs.
void onAuthLoginResult(AuthUiState& state,
                       const LoginResultMsg& login,
                       std::deque<std::string>& logs);

// Renders/updates auth UI. Returns true when auth screen consumed the frame.
bool tickAndDrawAuthUi(AuthUiState& state,
                       Mailbox& mailbox,
                       Font ui_font,
                       std::deque<std::string>& logs);

} // namespace client
