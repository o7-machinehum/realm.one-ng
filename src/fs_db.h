#pragma once
#include <filesystem>
#include <optional>
#include <string>

struct CharacterRecord {
    std::string name;
    std::string password; // plaintext for now
    std::string room;
    int x = 0; // tile coords
    int y = 0;
};

class FsDb {
public:
    explicit FsDb(std::filesystem::path root);

    std::optional<CharacterRecord> loadCharacter(const std::string& name) const;
    bool saveCharacter(const CharacterRecord& rec) const;
    bool characterExists(const std::string& name) const;

    std::optional<std::string> loadRoomTmx(const std::string& roomName) const;

private:
    std::filesystem::path root_;
    std::filesystem::path charPath(const std::string& name) const;
    std::filesystem::path roomPath(const std::string& roomName) const;
};
