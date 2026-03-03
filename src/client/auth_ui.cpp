#include "auth_ui.h"

#include "auth_crypto.h"
#include "client_ui_primitives.h"
#include "client_support.h"
#include "local_keys.h"
#include "msg.h"
#include "raylib.h"

#include <deque>
#include <string>

namespace {

constexpr int kMaxNameChars = 24;

// Handles create-account flow: key generation, local save, and signed request enqueue.
void runCreateAccount(client::AuthUiState& state, Mailbox& mailbox) {
    std::string pub_hex;
    std::string priv_hex;
    if (state.create_name.empty()) {
        state.auth_message = "name is required";
        return;
    }
    if (!generateEd25519KeypairHex(pub_hex, priv_hex)) {
        state.auth_message = "key generation failed";
        return;
    }

    client::LocalKeyPair kp{state.create_name, pub_hex, priv_hex};
    std::string err;
    if (!client::saveLocalKeyPair(kp, err)) {
        state.auth_message = err;
        return;
    }

    LoginMsg m{};
    m.user = state.create_name;
    m.public_key_hex = pub_hex;
    m.create_account = true;
    if (!signEd25519Hex(priv_hex, makeAuthPayload(m.user, m.public_key_hex, m.create_account), m.signature_hex)) {
        state.auth_message = "failed to sign create request";
        return;
    }

    mailbox.push(MsgType::Login, m);
    state.auth_waiting = true;
    state.auth_message = "creating account...";
    state.login_name = state.create_name;
    state.local_users = client::listLocalKeyUsernames();
    state.show_create_modal = false;
    state.auth_focus = 1;
}

// Handles login flow using the local private key for the selected username.
void runLogin(client::AuthUiState& state, Mailbox& mailbox) {
    client::LocalKeyPair kp{};
    std::string err;
    if (!client::loadLocalKeyPair(state.login_name, kp, err)) {
        state.auth_message = err;
        return;
    }

    LoginMsg m{};
    m.user = state.login_name;
    m.public_key_hex = kp.public_key_hex;
    m.create_account = false;
    if (!signEd25519Hex(kp.private_key_hex, makeAuthPayload(m.user, m.public_key_hex, m.create_account), m.signature_hex)) {
        state.auth_message = "failed to sign login request";
        return;
    }

    mailbox.push(MsgType::Login, m);
    state.auth_waiting = true;
    state.auth_message = "logging in...";
}

} // namespace

namespace client {

void initAuthUi(AuthUiState& state) {
    state = AuthUiState{};
    state.local_users = listLocalKeyUsernames();
    state.login_bg_tex = LoadTexture("game/assets/ui/login_bg.png");
}

void shutdownAuthUi(AuthUiState& state) {
    if (state.login_bg_tex.id != 0) {
        UnloadTexture(state.login_bg_tex);
    }
    state.login_bg_tex = Texture2D{};
}

void onAuthLoginResult(AuthUiState& state,
                       const LoginResultMsg& login,
                       std::deque<std::string>& logs) {
    state.auth_waiting = false;
    state.auth_message = login.message;
    if (login.ok) {
        state.auth_done = true;
        state.login_name = login.user;
        pushBounded(logs, "Logged in as " + login.user);
    } else {
        pushBounded(logs, "Login failed: " + login.message);
    }
}

bool tickAndDrawAuthUi(AuthUiState& state,
                       Mailbox& mailbox,
                       Font ui_font,
                       std::deque<std::string>& logs) {
    if (state.auth_done) return false;

    if (state.auth_focus == 1) appendUsernameInputChars(state.login_name, kMaxNameChars);
    if (state.auth_focus == 2) appendUsernameInputChars(state.create_name, kMaxNameChars);
    if (IsKeyPressed(KEY_BACKSPACE)) {
        if (state.auth_focus == 1 && !state.login_name.empty()) state.login_name.pop_back();
        if (state.auth_focus == 2 && !state.create_name.empty()) state.create_name.pop_back();
    }

    BeginDrawing();
    ClearBackground(BLACK);

    {
        const float sw = static_cast<float>(GetScreenWidth());
        const float sh = static_cast<float>(GetScreenHeight());
        const float tw = static_cast<float>(state.login_bg_tex.width);
        const float th = static_cast<float>(state.login_bg_tex.height);
        const float scale = std::max(sw / tw, sh / th);
        const float dw = tw * scale;
        const float dh = th * scale;
        Rectangle src{0.0f, 0.0f, tw, th};
        Rectangle dst{(sw - dw) * 0.5f, (sh - dh) * 0.5f, dw, dh};
        DrawTexturePro(state.login_bg_tex, src, dst, Vector2{0, 0}, 0.0f, WHITE);
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 95});
    }
    const float panel_w = 640.0f;
    const float panel_h = 360.0f;
    const float panel_x = 24.0f;
    const float panel_y = std::max(24.0f, static_cast<float>(GetScreenHeight()) - panel_h - 24.0f);
    const Rectangle panel{panel_x, panel_y, panel_w, panel_h};
    drawUiPanel(panel, Color{24, 28, 36, 250}, Color{88, 100, 126, 255}, 2.0f);
    DrawTextEx(ui_font, "The Island", Vector2{panel.x + 24.0f, panel.y + 20.0f}, 36.0f, 1.0f, RAYWHITE);
    DrawTextEx(ui_font, "Name", Vector2{panel.x + 24.0f, panel.y + 86.0f}, 22.0f, 1.0f, LIGHTGRAY);
    const Rectangle name_box{panel.x + 24.0f, panel.y + 116.0f, panel.width - 48.0f, 48.0f};
    if (CheckCollisionPointRec(GetMousePosition(), name_box) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) state.auth_focus = 1;
    drawUiPanel(name_box,
                state.auth_focus == 1 ? Color{40, 47, 61, 255} : Color{31, 37, 49, 255},
                Color{98, 110, 136, 255},
                1.0f);
    drawUiTextInputLine(ui_font,
                        state.login_name,
                        name_box.x + 12.0f,
                        name_box.y + 11.0f,
                        24.0f,
                        "",
                        state.auth_focus == 1,
                        RAYWHITE,
                        RAYWHITE);

    if (!state.local_users.empty()) {
        DrawTextEx(ui_font, "Local identities", Vector2{panel.x + 24.0f, panel.y + 170.0f}, 16.0f, 1.0f, LIGHTGRAY);
        float ux = panel.x + 150.0f;
        const float uy = panel.y + 166.0f;
        for (size_t i = 0; i < state.local_users.size() && i < 4; ++i) {
            const std::string& u = state.local_users[i];
            const Vector2 s = MeasureTextEx(ui_font, u.c_str(), 16.0f, 1.0f);
            const Rectangle r{ux, uy, std::max(70.0f, s.x + 18.0f), 24.0f};
            const bool active = (u == state.login_name);
            DrawRectangleRec(r, active ? Color{78, 102, 146, 255} : Color{44, 53, 72, 255});
            DrawRectangleLinesEx(r, 1.0f, Color{102, 118, 148, 255});
            DrawTextEx(ui_font, u.c_str(), Vector2{r.x + 9.0f, r.y + 3.0f}, 16.0f, 1.0f, RAYWHITE);
            if (CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                state.login_name = u;
                state.auth_focus = 1;
            }
            ux += r.width + 8.0f;
        }
    }

    const float by = panel.y + 196.0f;
    const Rectangle b_login{panel.x + 24.0f, by, 180.0f, 56.0f};
    const Rectangle b_create{panel.x + 228.0f, by, 180.0f, 56.0f};
    const Rectangle b_help{panel.x + 432.0f, by, 180.0f, 56.0f};

    if (!state.auth_waiting && drawUiButton(b_login, ui_font, "Login")) {
        runLogin(state, mailbox);
    }
    if (!state.auth_waiting && drawUiButton(b_create, ui_font, "Create Account")) {
        state.show_create_modal = true;
        state.create_name = state.login_name;
        state.auth_focus = 2;
    }
    if (drawUiButton(b_help, ui_font, "Help")) {
        state.show_help_modal = !state.show_help_modal;
    }

    if (!state.auth_message.empty()) {
        DrawTextEx(ui_font, state.auth_message.c_str(), Vector2{panel.x + 24.0f, panel.y + 278.0f}, 20.0f, 1.0f, YELLOW);
    }

    if (state.show_help_modal) {
        const Rectangle modal{panel.x + 58.0f, panel.y + 58.0f, panel.width - 116.0f, panel.height - 116.0f};
        drawUiPanel(modal, Color{12, 14, 20, 245}, Color{122, 132, 156, 255}, 1.0f);
        DrawTextEx(ui_font, "Help", Vector2{modal.x + 14.0f, modal.y + 12.0f}, 26.0f, 1.0f, RAYWHITE);
        DrawTextEx(ui_font, "Create account makes an Ed25519 keypair.", Vector2{modal.x + 14.0f, modal.y + 52.0f}, 18.0f, 1.0f, LIGHTGRAY);
        DrawTextEx(ui_font, "Login uses your local private key signature.", Vector2{modal.x + 14.0f, modal.y + 78.0f}, 18.0f, 1.0f, LIGHTGRAY);
        DrawTextEx(ui_font, "Server binds username -> public key.", Vector2{modal.x + 14.0f, modal.y + 104.0f}, 18.0f, 1.0f, LIGHTGRAY);
        if (drawUiButton(Rectangle{modal.x + modal.width - 110.0f, modal.y + modal.height - 52.0f, 90.0f, 36.0f}, ui_font, "close", 20.0f)) {
            state.show_help_modal = false;
        }
    }

    if (state.show_create_modal) {
        const Rectangle modal{panel.x + 48.0f, panel.y + 40.0f, panel.width - 96.0f, panel.height - 80.0f};
        drawUiPanel(modal, Color{10, 12, 18, 250}, Color{122, 132, 156, 255}, 1.0f);
        DrawTextEx(ui_font, "Create account", Vector2{modal.x + 16.0f, modal.y + 12.0f}, 28.0f, 1.0f, RAYWHITE);
        const Rectangle create_box{modal.x + 16.0f, modal.y + 68.0f, modal.width - 32.0f, 46.0f};
        if (CheckCollisionPointRec(GetMousePosition(), create_box) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) state.auth_focus = 2;
        drawUiPanel(create_box,
                    state.auth_focus == 2 ? Color{40, 47, 61, 255} : Color{31, 37, 49, 255},
                    Color{98, 110, 136, 255},
                    1.0f);
        drawUiTextInputLine(ui_font,
                            state.create_name,
                            create_box.x + 10.0f,
                            create_box.y + 10.0f,
                            24.0f,
                            "",
                            state.auth_focus == 2,
                            RAYWHITE,
                            RAYWHITE);
        if (drawUiButton(Rectangle{modal.x + 16.0f, modal.y + modal.height - 54.0f, 130.0f, 38.0f}, ui_font, "create", 20.0f)) {
            runCreateAccount(state, mailbox);
        }
        if (drawUiButton(Rectangle{modal.x + 160.0f, modal.y + modal.height - 54.0f, 130.0f, 38.0f}, ui_font, "cancel", 20.0f)) {
            state.show_create_modal = false;
            state.auth_focus = 1;
        }
    }

    EndDrawing();
    return true;
}

} // namespace client
