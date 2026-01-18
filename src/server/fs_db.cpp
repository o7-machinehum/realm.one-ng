#include "fs_db.h"
#include <fstream>
#include <sstream>

static std::string trim(std::string s) {
    auto ws = [](unsigned char c){ return c <= 32; };
    while (!s.empty() && ws((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && ws((unsigned char)s.back())) s.pop_back();
    return s;
}

FsDb::FsDb(std::filesystem::path root) : root_(std::move(root)) {
    std::filesystem::create_directories(root_ / "characters");
    std::filesystem::create_directories(root_ / "rooms");
}

std::filesystem::path FsDb::charPath(const std::string& name) const {
    return root_ / "characters" / (name + ".txt");
}

std::filesystem::path FsDb::roomPath(const std::string& roomName) const {
    return root_ / "rooms" / (roomName + ".tmx");
}

bool FsDb::characterExists(const std::string& name) const {
    return std::filesystem::exists(charPath(name));
}

std::optional<CharacterRecord> FsDb::loadCharacter(const std::string& name) const {
    std::ifstream f(charPath(name));
    if (!f) return std::nullopt;

    CharacterRecord rec;
    rec.name = name;

    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty()) continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto k = trim(line.substr(0, eq));
        auto v = trim(line.substr(eq + 1));
        if (k == "password") rec.password = v;
        else if (k == "room") rec.room = v;
        else if (k == "x") rec.x = std::stoi(v);
        else if (k == "y") rec.y = std::stoi(v);
    }

    if (rec.room.empty()) rec.room = "d1";
    return rec;
}

bool FsDb::saveCharacter(const CharacterRecord& rec) const {
    auto p = charPath(rec.name);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << "password=" << rec.password << "\n";
    f << "room=" << rec.room << "\n";
    f << "x=" << rec.x << "\n";
    f << "y=" << rec.y << "\n";
    return true;
}

std::optional<std::string> FsDb::loadRoomTmx(const std::string& roomName) const {
    auto p = roomPath(roomName);
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
