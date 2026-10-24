#include "core/sign/pkcs12.h"

#include <openssl/err.h>
#include <openssl/pkcs12.h>

#include <string>

namespace ot::sign {

namespace {

// Пароль для OpenSSL: пустой — сначала nullptr (как PKCS12_parse), затем "".
bool macMatches(PKCS12* p12, const std::string& pass) {
    if (PKCS12_mac_present(p12) == 0) {
        return true;
    }
    if (pass.empty()) {
        return PKCS12_verify_mac(p12, nullptr, 0) == 1 || PKCS12_verify_mac(p12, "", 0) == 1;
    }
    return PKCS12_verify_mac(p12, pass.c_str(), static_cast<int>(pass.size())) == 1;
}

struct Parsed {
    EVP_PKEY* key = nullptr;
    X509* cert = nullptr;
    STACK_OF(X509) * ca = nullptr;
};

bool tryParse(PKCS12* p12, const std::string& pass, Parsed& out) {
    out = {};
    const int ok = PKCS12_parse(p12, pass.c_str(), &out.key, &out.cert, &out.ca);
    return ok == 1;
}

}  // namespace

SigningKey loadPkcs12(std::span<const std::uint8_t> pfx, std::string_view passwordUtf8) {
    ERR_clear_error();
    if (pfx.empty()) {
        throw Pkcs12Error(Pkcs12Status::Damaged, "PKCS#12: empty file");
    }
    const unsigned char* p = pfx.data();
    Ossl<PKCS12> p12(d2i_PKCS12(nullptr, &p, static_cast<long>(pfx.size())));
    if (!p12) {
        throw Pkcs12Error(Pkcs12Status::Damaged, OsslError::fromQueue("d2i_PKCS12").what());
    }
    const std::string pass(passwordUtf8);
    if (!macMatches(p12.get(), pass)) {
        ERR_clear_error();
        throw Pkcs12Error(Pkcs12Status::BadPassword, "PKCS#12: MAC verification failed (wrong password)");
    }
    Parsed parsed;
    if (!tryParse(p12.get(), pass, parsed)) {
        // RC2-40/RC4 — только провайдером legacy (в статическом libcrypto он встроен).
        const std::string first = OsslError::fromQueue("PKCS12_parse").what();
        if (!ossl::ensureLegacyProvider() || !tryParse(p12.get(), pass, parsed)) {
            const OsslError e = OsslError::fromQueue("PKCS12_parse");
            // Без MAC неверный пароль виден только здесь: расшифровка даёт мусор.
            const bool noMac = PKCS12_mac_present(p12.get()) == 0;
            throw Pkcs12Error(noMac ? Pkcs12Status::BadPassword : Pkcs12Status::Unsupported,
                              first + "; with legacy provider: " + e.what());
        }
    }
    SigningKey out;
    out.key.reset(parsed.key);
    out.cert.reset(parsed.cert);
    out.chain.reset(parsed.ca != nullptr ? parsed.ca : sk_X509_new_null());
    if (!out.chain) {
        throw OsslError::fromQueue("sk_X509_new_null");
    }
    if (!out.key) {
        throw Pkcs12Error(Pkcs12Status::NoKey, "PKCS#12: no private key");
    }
    if (!out.cert) {
        throw Pkcs12Error(Pkcs12Status::NoCertificate, "PKCS#12: no certificate for the private key");
    }
    if (X509_check_private_key(out.cert.get(), out.key.get()) != 1) {
        ERR_clear_error();
        throw Pkcs12Error(Pkcs12Status::NoCertificate, "PKCS#12: certificate does not match the private key");
    }
    int len = 0;
    if (const unsigned char* alias = X509_alias_get0(out.cert.get(), &len); alias != nullptr && len > 0) {
        out.friendlyName.assign(reinterpret_cast<const char*>(alias), static_cast<std::size_t>(len));
    }
    ERR_clear_error();
    return out;
}

}  // namespace ot::sign
