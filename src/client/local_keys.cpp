#include "local_keys.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

namespace client {

namespace {

// Normalizes username text into a filesystem-safe basename.
std::string safeUserFile(std::string s) {
    for (char& c : s) {
        const bool ok = (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') ||
                        c == '_' || c == '-';
        if (!ok) c = '_';
    }
    return s;
}

// Returns the directory used for local auth key storage.
fs::path keysDir() {
    return fs::path("data") / "keys";
}

// Builds the per-user key file path.
fs::path keyPathForUser(const std::string& username) {
    return keysDir() / (safeUserFile(username) + ".key");
}

} // namespace

// Persists a generated keypair for one local username.
bool saveLocalKeyPair(const LocalKeyPair& kp, std::string& err) {
    err.clear();
    if (kp.username.empty() || kp.public_key_hex.empty() || kp.private_key_hex.empty()) {
        err = "missing key fields";
        return false;
    }

    std::error_code ec;
    fs::create_directories(keysDir(), ec);
    if (ec) {
        err = "failed to create key dir";
        return false;
    }

    std::ofstream out(keyPathForUser(kp.username), std::ios::binary | std::ios::trunc);
    if (!out) {
        err = "failed to open key file";
        return false;
    }
    out << "username=" << kp.username << "\n";
    out << "public=" << kp.public_key_hex << "\n";
    out << "private=" << kp.private_key_hex << "\n";
    return true;
}

// Loads an existing keypair for one local username.
bool loadLocalKeyPair(const std::string& username, LocalKeyPair& out, std::string& err) {
    err.clear();
    out = LocalKeyPair{};

    std::ifstream in(keyPathForUser(username), std::ios::binary);
    if (!in) {
        err = "no local key for username";
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("username=", 0) == 0) out.username = line.substr(9);
        else if (line.rfind("public=", 0) == 0) out.public_key_hex = line.substr(7);
        else if (line.rfind("private=", 0) == 0) out.private_key_hex = line.substr(8);
    }
    if (out.username.empty()) out.username = username;
    if (out.public_key_hex.empty() || out.private_key_hex.empty()) {
        err = "invalid key file";
        return false;
    }
    return true;
}

// Lists all local usernames that currently have stored key files.
std::vector<std::string> listLocalKeyUsernames() {
    std::vector<std::string> out;
    std::error_code ec;
    const fs::path dir = keysDir();
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return out;

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".key") continue;
        const std::string user = entry.path().stem().string();
        if (!user.empty()) out.push_back(user);
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace client
