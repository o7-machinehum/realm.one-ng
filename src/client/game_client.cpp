#include "game_client.h"

#include "raylib.h"

#include "auth_screen.h"
#include "cube_renderer.h"
#include "net_client.h"
#include "net_msgs.h"
#include "voxel_world.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <unordered_map>

namespace gc {

namespace {

constexpr float kMoveRepeatInitialMs = 220.0f;
constexpr float kMoveRepeatMs        = 110.0f;
constexpr float kCameraLerp          = 8.0f;
constexpr int   kPlayerSpriteW       = 16;
constexpr int   kPlayerSpriteH       = 32;
constexpr int   kFadeRadiusXY        = 1;
constexpr uint8_t kFadeAlpha         = 100;
constexpr Color  kBackgroundColor    = {20, 24, 32, 255};

struct SpriteRect { int x, y, w, h; };

// character.png layout for player_1: 4 direction rows top→bottom (S, E, N, W),
// each row 32 px tall with 4 frames of 16 px across.
SpriteRect spriteForFacing(uint8_t facing) {
    constexpr std::array<SpriteRect, 4> kByFacing{{
        {0, 0 * kPlayerSpriteH, kPlayerSpriteW, kPlayerSpriteH},  // South
        {0, 1 * kPlayerSpriteH, kPlayerSpriteW, kPlayerSpriteH},  // East
        {0, 2 * kPlayerSpriteH, kPlayerSpriteW, kPlayerSpriteH},  // North
        {0, 3 * kPlayerSpriteH, kPlayerSpriteW, kPlayerSpriteH},  // West
    }};
    if (facing >= kByFacing.size()) return kByFacing[0];
    return kByFacing[facing];
}

class MoveRepeater {
public:
    bool tick(bool key_down, float dt_ms) {
        if (!key_down) { primed_ = false; timer_ms_ = 0.0f; return false; }
        if (!primed_)  { primed_ = true; timer_ms_ = kMoveRepeatInitialMs; return true; }
        timer_ms_ -= dt_ms;
        if (timer_ms_ <= 0.0f) { timer_ms_ = kMoveRepeatMs; return true; }
        return false;
    }
private:
    float timer_ms_ = 0.0f;
    bool  primed_   = false;
};

bool loadWorldOrReport(voxel::World& world, const std::string& path) {
    std::string err;
    if (voxel::load(world, path, &err)) return true;
    std::fprintf(stderr, "FATAL: load %s: %s\n", path.c_str(), err.c_str());
    return false;
}

Texture2D loadCharacterTextureOrReport() {
    Texture2D tex = LoadTexture("game/assets/art/characters/character.png");
    if (tex.id == 0) {
        std::fprintf(stderr, "FATAL: failed to load character.png\n");
        return tex;
    }
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    return tex;
}

void lerpToward(float& current, float target, float dt) {
    const float k = std::min(1.0f, kCameraLerp * dt);
    current += (target - current) * k;
}

} // namespace

struct GameClient::Impl {
    ClientOptions opts;
    voxel::World world;
    Texture2D character_tex{};
    cubes::TextureCache cube_textures;
    cubes::StepParams steps = cubes::kDefaultSteps;
    cubes::RenderParams render_params = cubes::kDefaultRender;
    cubes::Camera camera{};
    netc::NetClient net;

    uint32_t my_id = 0;
    std::unordered_map<uint32_t, net::PlayerSnapshot> players;
    bool camera_initialized = false;

    MoveRepeater rep_w, rep_a, rep_s, rep_d;

    explicit Impl(ClientOptions o) : opts(std::move(o)) {}
    ~Impl() {
        net.disconnect();
        if (character_tex.id != 0) UnloadTexture(character_tex);
        cube_textures.unloadAll();
        if (IsWindowReady()) CloseWindow();
    }

    bool init() {
        if (!loadWorldOrReport(world, opts.world_path)) return false;

        InitWindow(opts.window_w, opts.window_h, "Cube Client");
        SetWindowState(FLAG_WINDOW_RESIZABLE);
        SetTargetFPS(60);

        character_tex = loadCharacterTextureOrReport();
        if (character_tex.id == 0) return false;

        if (!net.connect(opts.host, opts.port)) {
            std::fprintf(stderr, "FATAL: cannot connect to %s:%u\n", opts.host.c_str(), opts.port);
            return false;
        }

        const auto auth = authui::runAuthScreen(net);
        if (!auth) return false;            // window closed
        opts.player_name = auth->username;
        return true;
    }

    void processIncoming() {
        net.pump(0);
        while (auto msg = net.pop()) {
            std::visit([&](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, netc::WelcomeEvent>) {
                    my_id = v.your_id;
                } else if constexpr (std::is_same_v<T, net::PlayerSnapshot>) {
                    players[v.id] = v;
                } else if constexpr (std::is_same_v<T, net::PlayerLeaveEvent>) {
                    players.erase(v.id);
                } else if constexpr (std::is_same_v<T, net::LoginResultEvent>) {
                    // Auth screen consumes these; ignored once in game.
                }
            }, *msg);
        }
    }

    const net::PlayerSnapshot* localPlayer() const {
        const auto it = players.find(my_id);
        return (it == players.end()) ? nullptr : &it->second;
    }

    void sendInputIntents(float dt_ms) {
        if (rep_w.tick(IsKeyDown(KEY_W) || IsKeyDown(KEY_UP),    dt_ms)) net.sendMove(0, -1);
        if (rep_s.tick(IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN),  dt_ms)) net.sendMove(0, +1);
        if (rep_a.tick(IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT),  dt_ms)) net.sendMove(-1, 0);
        if (rep_d.tick(IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT), dt_ms)) net.sendMove(+1, 0);
    }

    void updateCamera(const net::PlayerSnapshot& me, float dt) {
        const float tx = static_cast<float>(me.x);
        const float ty = static_cast<float>(me.y);
        if (!camera_initialized) {
            camera.center_x = tx;
            camera.center_y = ty;
            camera_initialized = true;
            return;
        }
        lerpToward(camera.center_x, tx, dt);
        lerpToward(camera.center_y, ty, dt);
    }

    cubes::FadeRule fadeRuleFor(const net::PlayerSnapshot* me) const {
        cubes::FadeRule fade{};
        if (!me) return fade;
        fade.enabled   = true;
        fade.center_x  = me->x;
        fade.center_y  = me->y;
        fade.center_z  = me->z;
        fade.radius_xy = kFadeRadiusXY;
        fade.alpha     = kFadeAlpha;
        return fade;
    }

    cubes::Projection projection() const {
        return cubes::Projection{
            steps, render_params, camera,
            GetScreenWidth(), GetScreenHeight(),
        };
    }

    void drawPlayer(const net::PlayerSnapshot& p, const cubes::Projection& proj) const {
        const Vector2 pos = cubes::worldToScreen(
            static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z), proj);
        const float scale = render_params.scale;
        const float dst_w = kPlayerSpriteW * scale;
        const float dst_h = kPlayerSpriteH * scale;
        const SpriteRect s = spriteForFacing(p.facing);
        const Rectangle src{
            static_cast<float>(s.x), static_cast<float>(s.y),
            static_cast<float>(s.w), static_cast<float>(s.h)};
        const Rectangle dst{pos.x - dst_w * 0.5f, pos.y - dst_h, dst_w, dst_h};
        DrawTexturePro(character_tex, src, dst, Vector2{0, 0}, 0.0f, WHITE);
    }

    void drawDebugHud(const net::PlayerSnapshot* me) const {
        if (!me) {
            DrawText("waiting for server snapshot...", 8, 8, 16, RAYWHITE);
            return;
        }
        DrawText(TextFormat("id=%u  pos=(%d,%d,%d)  players=%d  WASD to walk",
                            my_id, me->x, me->y, me->z,
                            static_cast<int>(players.size())),
                 8, 8, 16, RAYWHITE);
    }

    void renderFrame() {
        const auto* me = localPlayer();
        const auto proj = projection();
        const auto fade = fadeRuleFor(me);

        BeginDrawing();
        ClearBackground(kBackgroundColor);

        cubes::drawWorld(world, proj, cube_textures, fade,
                         [&](int diag, int z) {
                             for (auto& kv : players) {
                                 const auto& p = kv.second;
                                 if ((p.x + p.y) == diag && p.z == z) drawPlayer(p, proj);
                             }
                         });

        drawDebugHud(me);
        EndDrawing();
    }

    void tickOnce(float dt) {
        const float dt_ms = dt * 1000.0f;
        processIncoming();
        if (const auto* me = localPlayer()) {
            sendInputIntents(dt_ms);
            updateCamera(*me, dt);
        }
        renderFrame();
    }
};

GameClient::GameClient(ClientOptions opts) : impl_(std::make_unique<Impl>(std::move(opts))) {}
GameClient::~GameClient() = default;

bool GameClient::init() { return impl_->init(); }

void GameClient::runUntilClosed() {
    while (!WindowShouldClose()) impl_->tickOnce(GetFrameTime());
}

} // namespace gc
