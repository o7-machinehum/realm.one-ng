#pragma once

#include <string>

std::string makeAuthPayload(const std::string& username,
                            const std::string& public_key_hex,
                            bool create_account);

bool generateEd25519KeypairHex(std::string& out_public_hex, std::string& out_private_hex);

bool signEd25519Hex(const std::string& private_key_hex,
                    const std::string& message,
                    std::string& out_signature_hex);

bool verifyEd25519Hex(const std::string& public_key_hex,
                      const std::string& message,
                      const std::string& signature_hex);
