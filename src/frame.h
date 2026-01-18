#pragma once

#include <raylib.h>
#include <string>

class Sprites {
    Sprite(std::string docs);
}

class sprite {
    std::vec<frame> north;
    std::vec<frame> south;
    std::vec<frame> east;
    std::vec<frame> west;

public:
    Sprite(Texture2D tex*, std::string name);

}

class Frame {
    Frame(Texture2D tex*)
}

class TileSet {


}
