#pragma once

#include <string>

struct ProgressionSettings {
    // EXP needed to go from level L to L+1:
    // need(L) = a*L*L + b*L + c
    int exp_per_level_a = 0;
    int exp_per_level_b = 50;
    int exp_per_level_c = 100;
};

struct GameplaySettings {
    int monster_respawn_ms = 15000;
};

struct GlobalSettings {
    ProgressionSettings progression;
    GameplaySettings gameplay;
};

GlobalSettings loadGlobalSettings(const std::string& path);
