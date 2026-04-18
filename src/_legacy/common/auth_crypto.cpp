#include "auth_crypto.h"

#include <openssl/evp.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {

// Encodes a nibble as a lowercase hex char.
char nibbleToHex(uint8_t n) {
    return static_cast<char>((n < 10) ? ('0' + n) : ('a' + (n - 10)));
}

// Decodes one hex char into a nibble value.
int hexToNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Converts raw bytes into lowercase hex.
std::string toHex(const uint8_t* data, size_t len) {
    std::string out;
    out.resize(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out[(i * 2)] = nibbleToHex(static_cast<uint8_t>((data[i] >> 4) & 0x0F));
        out[(i * 2) + 1] = nibbleToHex(static_cast<uint8_t>(data[i] & 0x0F));
    }
    return out;
}

// Converts lowercase/uppercase hex into bytes.
bool fromHex(const std::string& hex, std::vector<uint8_t>& out) {
    if (hex.empty() || (hex.size() % 2) != 0) return false;
    out.clear();
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = hexToNibble(hex[i]);
        const int lo = hexToNibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

} // namespace

// Builds a canonical payload that clients sign and servers verify.
std::string makeAuthPayload(const std::string& username,
                            const std::string& public_key_hex,
                            bool create_account) {
    return std::string("the_island_auth_v1|")
        + (create_account ? "create" : "login")
        + "|" + username + "|" + public_key_hex;
}

// Generates a fresh Ed25519 keypair and returns both keys as hex.
bool generateEd25519KeypairHex(std::string& out_public_hex, std::string& out_private_hex) {
    out_public_hex.clear();
    out_private_hex.clear();

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    if (!ctx) return false;

    EVP_PKEY* pkey = nullptr;
    bool ok = false;
    if (EVP_PKEY_keygen_init(ctx) == 1 && EVP_PKEY_keygen(ctx, &pkey) == 1 && pkey) {
        std::array<uint8_t, 32> pub{};
        std::array<uint8_t, 32> priv{};
        size_t pub_len = pub.size();
        size_t priv_len = priv.size();
        if (EVP_PKEY_get_raw_public_key(pkey, pub.data(), &pub_len) == 1 &&
            EVP_PKEY_get_raw_private_key(pkey, priv.data(), &priv_len) == 1 &&
            pub_len == pub.size() && priv_len == priv.size()) {
            out_public_hex = toHex(pub.data(), pub.size());
            out_private_hex = toHex(priv.data(), priv.size());
            ok = true;
        }
    }

    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);
    return ok;
}

// Signs an auth payload using an Ed25519 private key (hex encoded).
bool signEd25519Hex(const std::string& private_key_hex,
                    const std::string& message,
                    std::string& out_signature_hex) {
    out_signature_hex.clear();

    std::vector<uint8_t> priv;
    if (!fromHex(private_key_hex, priv) || priv.size() != 32) return false;

    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, priv.data(), priv.size());
    if (!pkey) return false;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        EVP_PKEY_free(pkey);
        return false;
    }

    std::array<uint8_t, 64> sig{};
    size_t sig_len = sig.size();
    const bool ok = EVP_DigestSignInit(mdctx, nullptr, nullptr, nullptr, pkey) == 1 &&
                    EVP_DigestSign(mdctx,
                                   sig.data(),
                                   &sig_len,
                                   reinterpret_cast<const uint8_t*>(message.data()),
                                   message.size()) == 1;
    if (ok) {
        out_signature_hex = toHex(sig.data(), sig_len);
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    return ok;
}

// Verifies a signature over an auth payload using an Ed25519 public key (hex encoded).
bool verifyEd25519Hex(const std::string& public_key_hex,
                      const std::string& message,
                      const std::string& signature_hex) {
    std::vector<uint8_t> pub;
    std::vector<uint8_t> sig;
    if (!fromHex(public_key_hex, pub) || pub.size() != 32) return false;
    if (!fromHex(signature_hex, sig) || sig.size() != 64) return false;

    EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, pub.data(), pub.size());
    if (!pkey) return false;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        EVP_PKEY_free(pkey);
        return false;
    }

    const bool ok = EVP_DigestVerifyInit(mdctx, nullptr, nullptr, nullptr, pkey) == 1 &&
                    EVP_DigestVerify(mdctx,
                                     sig.data(),
                                     sig.size(),
                                     reinterpret_cast<const uint8_t*>(message.data()),
                                     message.size()) == 1;

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    return ok;
}
