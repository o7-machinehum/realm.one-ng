#include "auth_crypto.h"

#include <openssl/evp.h>

#include <array>
#include <cstdint>
#include <vector>

namespace authx {

namespace {

constexpr size_t kEd25519PublicLen  = 32;
constexpr size_t kEd25519PrivateLen = 32;
constexpr size_t kEd25519SignatureLen = 64;

char nibbleToHex(uint8_t n) {
    return static_cast<char>((n < 10) ? ('0' + n) : ('a' + (n - 10)));
}

int hexToNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string toHex(const uint8_t* data, size_t len) {
    std::string out;
    out.resize(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out[i * 2]     = nibbleToHex((data[i] >> 4) & 0x0F);
        out[i * 2 + 1] = nibbleToHex(data[i] & 0x0F);
    }
    return out;
}

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

// RAII wrappers around OpenSSL handles.
struct EvpKeyCtx {
    EVP_PKEY_CTX* p;
    explicit EvpKeyCtx(int id) : p(EVP_PKEY_CTX_new_id(id, nullptr)) {}
    ~EvpKeyCtx() { if (p) EVP_PKEY_CTX_free(p); }
    explicit operator bool() const { return p != nullptr; }
};

struct EvpKey {
    EVP_PKEY* p;
    EvpKey() : p(nullptr) {}
    explicit EvpKey(EVP_PKEY* k) : p(k) {}
    ~EvpKey() { if (p) EVP_PKEY_free(p); }
    EvpKey(const EvpKey&) = delete;
    EvpKey& operator=(const EvpKey&) = delete;
    explicit operator bool() const { return p != nullptr; }
};

struct EvpMdCtx {
    EVP_MD_CTX* p;
    EvpMdCtx() : p(EVP_MD_CTX_new()) {}
    ~EvpMdCtx() { if (p) EVP_MD_CTX_free(p); }
    explicit operator bool() const { return p != nullptr; }
};

} // namespace

std::string makeAuthPayload(const std::string& username,
                            const std::string& public_key_hex,
                            bool create_account) {
    return std::string("the_island_auth_v1|")
        + (create_account ? "create" : "login")
        + "|" + username + "|" + public_key_hex;
}

std::optional<KeyPairHex> generateEd25519KeyPair() {
    EvpKeyCtx ctx(EVP_PKEY_ED25519);
    if (!ctx) return std::nullopt;
    if (EVP_PKEY_keygen_init(ctx.p) != 1) return std::nullopt;

    EVP_PKEY* raw = nullptr;
    if (EVP_PKEY_keygen(ctx.p, &raw) != 1 || !raw) return std::nullopt;
    EvpKey pkey(raw);

    std::array<uint8_t, kEd25519PublicLen>  pub{};
    std::array<uint8_t, kEd25519PrivateLen> priv{};
    size_t pub_len  = pub.size();
    size_t priv_len = priv.size();
    if (EVP_PKEY_get_raw_public_key (pkey.p, pub.data(),  &pub_len)  != 1 ||
        EVP_PKEY_get_raw_private_key(pkey.p, priv.data(), &priv_len) != 1 ||
        pub_len != pub.size() || priv_len != priv.size()) {
        return std::nullopt;
    }
    return KeyPairHex{toHex(pub.data(), pub.size()), toHex(priv.data(), priv.size())};
}

std::optional<std::string> signEd25519(const std::string& private_key_hex,
                                       const std::string& message) {
    std::vector<uint8_t> priv;
    if (!fromHex(private_key_hex, priv) || priv.size() != kEd25519PrivateLen) return std::nullopt;

    EvpKey pkey(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                             priv.data(), priv.size()));
    if (!pkey) return std::nullopt;

    EvpMdCtx mdctx;
    if (!mdctx) return std::nullopt;
    if (EVP_DigestSignInit(mdctx.p, nullptr, nullptr, nullptr, pkey.p) != 1) return std::nullopt;

    std::array<uint8_t, kEd25519SignatureLen> sig{};
    size_t sig_len = sig.size();
    if (EVP_DigestSign(mdctx.p, sig.data(), &sig_len,
                       reinterpret_cast<const uint8_t*>(message.data()),
                       message.size()) != 1) {
        return std::nullopt;
    }
    return toHex(sig.data(), sig_len);
}

bool verifyEd25519(const std::string& public_key_hex,
                   const std::string& message,
                   const std::string& signature_hex) {
    std::vector<uint8_t> pub;
    std::vector<uint8_t> sig;
    if (!fromHex(public_key_hex, pub) || pub.size() != kEd25519PublicLen)    return false;
    if (!fromHex(signature_hex, sig)  || sig.size() != kEd25519SignatureLen) return false;

    EvpKey pkey(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                            pub.data(), pub.size()));
    if (!pkey) return false;

    EvpMdCtx mdctx;
    if (!mdctx) return false;
    if (EVP_DigestVerifyInit(mdctx.p, nullptr, nullptr, nullptr, pkey.p) != 1) return false;

    return EVP_DigestVerify(mdctx.p, sig.data(), sig.size(),
                            reinterpret_cast<const uint8_t*>(message.data()),
                            message.size()) == 1;
}

} // namespace authx
