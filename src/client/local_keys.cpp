#include "local_keys.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace localkey {

namespace {

namespace fs = std::filesystem;

const char* const kKeyFileExtension = ".key";

bool isAllowedChar(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == '-';
}

std::string sanitizeForFilename(std::string s) {
    for (char& c : s) {
        if (!isAllowedChar(c)) c = '_';
    }
    return s;
}

fs::path keysDir() {
    return fs::path("data") / "keys";
}

fs::path keyPathForUsername(const std::string& username) {
    return keysDir() / (sanitizeForFilename(username) + kKeyFileExtension);
}

void setError(std::string* err, const char* msg) {
    if (err) *err = msg;
}

bool ensureKeysDir(std::string* err) {
    std::error_code ec;
    fs::create_directories(keysDir(), ec);
    if (ec) { setError(err, "failed to create key dir"); return false; }
    return true;
}

bool parseLine(const std::string& line, std::string& key, std::string& value) {
    const auto eq = line.find('=');
    if (eq == std::string::npos) return false;
    key   = line.substr(0, eq);
    value = line.substr(eq + 1);
    return true;
}

} // namespace

bool save(const StoredKey& kp, std::string* err) {
    if (kp.username.empty() || kp.public_key_hex.empty() || kp.private_key_hex.empty()) {
        setError(err, "missing key fields");
        return false;
    }
    if (!ensureKeysDir(err)) return false;

    std::ofstream out(keyPathForUsername(kp.username), std::ios::binary | std::ios::trunc);
    if (!out) { setError(err, "failed to open key file"); return false; }

    out << "username=" << kp.username        << "\n";
    out << "public="   << kp.public_key_hex  << "\n";
    out << "private="  << kp.private_key_hex << "\n";
    return out.good();
}

std::optional<StoredKey> load(const std::string& username, std::string* err) {
    std::ifstream in(keyPathForUsername(username));
    if (!in) { setError(err, "key file not found"); return std::nullopt; }

    StoredKey kp;
    std::string line;
    while (std::getline(in, line)) {
        std::string k, v;
        if (!parseLine(line, k, v)) continue;
        if      (k == "username") kp.username = v;
        else if (k == "public")   kp.public_key_hex = v;
        else if (k == "private")  kp.private_key_hex = v;
    }
    if (kp.username.empty() || kp.public_key_hex.empty() || kp.private_key_hex.empty()) {
        setError(err, "incomplete key file");
        return std::nullopt;
    }
    return kp;
}

std::vector<std::string> listUsernames() {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::exists(keysDir(), ec)) return out;
    for (const auto& entry : fs::directory_iterator(keysDir(), ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != kKeyFileExtension) continue;
        if (auto kp = load(entry.path().stem().string())) {
            out.push_back(kp->username);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace localkey
