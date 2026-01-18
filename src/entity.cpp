#include "entity.h"

Entity& Entities::create(const Sprite* sprite, int x, int y)
{
    Entity& e = entities_[_next_uid];
    e.uid = _next_uid;
    e.sprite = sprite;
    e.pos = {x, y};
	_next_uid++;
    return e;
}

Entity* Entities::get(uint64_t uid)
{
    auto it = entities_.find(uid);
    if (it == entities_.end()) return nullptr;
    return &it->second;
}

const Entity* Entities::get(uint64_t uid) const
{
    auto it = entities_.find(uid);
    if (it == entities_.end()) return nullptr;
    return &it->second;
}

void Entities::erase(uint64_t uid)
{
    entities_.erase(uid);
}
