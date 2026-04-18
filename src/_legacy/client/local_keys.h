#pragma once

#include <string>
#include <vector>

namespace client {

struct LocalKeyPair {
    std::string username;
    std::string public_key_hex;
    std::string private_key_hex;
};

bool saveLocalKeyPair(const LocalKeyPair& kp, std::string& err);
bool loadLocalKeyPair(const std::string& username, LocalKeyPair& out, std::string& err);
std::vector<std::string> listLocalKeyUsernames();

} // namespace client
