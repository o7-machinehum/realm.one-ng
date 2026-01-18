#pragma once

#include <cstdint>
#include <map>
#include <tuple>

#include "sprites.h"

// Maps source (spritesheet) to dest (player screen)
struct Map {
	Rectangle src;
	Rectangle dst;
	Vector2 orig;
};

class Entity {
	Dir dir = Dir::S;
	float scale = 2;
	float px_to_game = 16.0;

	// Get frame to draw from spritesheet
	Rectangle get_rect_src() {
		if(dir == Dir::N) return sprite->north.frames[0].rect();
		else if(dir == Dir::S) return sprite->south.frames[0].rect();
		else if(dir == Dir::E) return sprite->east.frames[0].rect();
		else return sprite->west.frames[0].rect();
	};

public:
    uint64_t uid = 0;
    const Sprite* sprite = nullptr;
    std::tuple<float,float> pos{0,0}; // Hold position in game units

	Map get_map() {
		auto [x, y] = pos;
		Map map;
		map.src = get_rect_src();
		map.dst = { x * px_to_game, y * px_to_game, map.src.width * scale, map.src.height * scale };
        map.orig = { map.dst.width / 2.0f, map.dst.height };
		return map;
	};
};

class Entities {
public:
    // Create or replace entity
    Entity& create(const Sprite* sprite, int x, int y);

    // Lookup (nullptr if not found)
    Entity* get(uint64_t uid);
    const Entity* get(uint64_t uid) const;

    // Remove
    void erase(uint64_t uid);

    // Simple iteration access
    auto& all() { return entities_; }
    const auto& all() const { return entities_; }

private:
    std::map<uint64_t, Entity> entities_;
	uint64_t _next_uid{1};
};
