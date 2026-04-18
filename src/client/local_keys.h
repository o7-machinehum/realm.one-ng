#pragma once

#include "auth_crypto.h"

#include <optional>
#include <string>
#include <vector>

namespace localkey {

struct StoredKey {
    std::string username;
    std::string public_key_hex;
    std::string private_key_hex;
};

bool save(const StoredKey& kp, std::string* err = nullptr);
std::optional<StoredKey> load(const std::string& username, std::string* err = nullptr);
std::vector<std::string> listUsernames();

} // namespace localkey
