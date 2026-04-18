#pragma once

#include <optional>
#include <string>

namespace authx {

struct KeyPairHex {
    std::string public_key;
    std::string private_key;
};

// Builds the canonical payload that clients sign and servers verify.
std::string makeAuthPayload(const std::string& username,
                            const std::string& public_key_hex,
                            bool create_account);

std::optional<KeyPairHex> generateEd25519KeyPair();

std::optional<std::string> signEd25519(const std::string& private_key_hex,
                                       const std::string& message);

bool verifyEd25519(const std::string& public_key_hex,
                   const std::string& message,
                   const std::string& signature_hex);

} // namespace authx
