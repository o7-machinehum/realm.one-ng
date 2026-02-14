#pragma once

#include <string>

namespace client {

struct UiSettings {
    float speech_bubble_alpha = 0.5f;    // 0..1
    float player_name_text_size = 13.0f; // base px before ui scale
    float monster_name_text_size = 16.0f;
    float npc_name_text_size = 13.0f;
    float speech_text_size = 16.0f;      // base px before ui scale
};

UiSettings loadUiSettings(const std::string& path);

} // namespace client
