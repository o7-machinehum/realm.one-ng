#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace authdb {

struct AuthOutcome {
    bool        success;
    std::string message;
};

struct PersistedState {
    int32_t x;
    int32_t y;
    int32_t z;
    uint8_t facing;
};

// SQLite-backed account directory. Stores username + Ed25519 public key, plus
// per-player world position. Phase >=2 will add columns (inventory, skills,
// equipment) via additive migrations.
class AuthDb {
public:
    explicit AuthDb(const std::string& db_path);
    ~AuthDb();
    AuthDb(const AuthDb&) = delete;
    AuthDb& operator=(const AuthDb&) = delete;

    AuthOutcome tryLogin(const std::string& username,
                         const std::string& public_key_hex,
                         bool create_account);

    std::optional<PersistedState> loadState(const std::string& username);
    bool saveState(const std::string& username, const PersistedState& state);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace authdb
