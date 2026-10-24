#include "core/sign/x509_info.h"

#include <openssl/err.h>
#include <openssl/pem.h>

#include <cstring>
#include <ctime>

namespace ot::sign {

std::string nameToString(const X509_NAME* name) {
    if (name == nullptr) {
        return {};
    }
    Ossl<BIO> bio = ossl::newMemBio();
    // RFC 2253 без экранирования октетов > 0x7F: кириллица остаётся UTF-8.
    X509_NAME_print_ex(bio.get(), name, 0, XN_FLAG_RFC2253 & ~ASN1_STRFLGS_ESC_MSB);
    const Bytes b = ossl::bioBytes(bio.get());
    return {b.begin(), b.end()};
}

std::string commonName(const X509_NAME* name) {
    if (name == nullptr) {
        return {};
    }
    const int idx = X509_NAME_get_index_by_NID(name, NID_commonName, -1);
    if (idx < 0) {
        return {};
    }
    unsigned char* utf8 = nullptr;
    const int len = ASN1_STRING_to_UTF8(&utf8, X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, idx)));
    if (len < 0) {
        ERR_clear_error();
        return {};
    }
    std::string out(reinterpret_cast<const char*>(utf8), static_cast<std::size_t>(len));
    OPENSSL_free(utf8);
    return out;
}

std::string serialHex(const ASN1_INTEGER* serial) {
    if (serial == nullptr) {
        return {};
    }
    Ossl<BIGNUM> bn(ASN1_INTEGER_to_BN(serial, nullptr));
    if (!bn) {
        ERR_clear_error();
        return {};
    }
    char* hex = BN_bn2hex(bn.get());
    std::string out = hex != nullptr ? hex : "";
    OPENSSL_free(hex);
    return out;
}

std::string timeToIso(const ASN1_TIME* time) {
    if (time == nullptr) {
        return {};
    }
    std::tm tm{};
    if (ASN1_TIME_to_tm(time, &tm) != 1) {
        ERR_clear_error();
        return {};
    }
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

CertSummary summarize(X509* cert) {
    CertSummary s;
    if (cert == nullptr) {
        return s;
    }
    s.subject = nameToString(X509_get_subject_name(cert));
    s.issuer = nameToString(X509_get_issuer_name(cert));
    s.commonName = commonName(X509_get_subject_name(cert));
    s.serial = serialHex(X509_get0_serialNumber(cert));
    s.notBefore = timeToIso(X509_get0_notBefore(cert));
    s.notAfter = timeToIso(X509_get0_notAfter(cert));
    if (EVP_PKEY* key = X509_get0_pubkey(cert)) {
        const int id = EVP_PKEY_get_base_id(key);
        s.keyAlgorithm = id == EVP_PKEY_RSA       ? "RSA"
                         : id == EVP_PKEY_RSA_PSS ? "RSA-PSS"
                         : id == EVP_PKEY_EC      ? "EC"
                         : id == EVP_PKEY_ED25519 ? "Ed25519"
                         : id == EVP_PKEY_DSA     ? "DSA"
                                                  : OBJ_nid2sn(id);
        s.keyBits = EVP_PKEY_get_bits(key);
    }
    ERR_clear_error();
    s.ca = X509_check_ca(cert) != 0;
    s.der = ossl::toDer(cert, i2d_X509, "i2d_X509");
    s.sha256 = ossl::sha256(s.der);
    return s;
}

Ossl<X509> loadCertificate(std::span<const std::uint8_t> derOrPem) {
    auto all = loadCertificates(derOrPem);
    if (all.empty()) {
        throw OsslError::fromQueue("certificate");
    }
    return std::move(all.front());
}

std::vector<Ossl<X509>> loadCertificates(std::span<const std::uint8_t> derOrPem) {
    std::vector<Ossl<X509>> out;
    static constexpr char kPem[] = "-----BEGIN";
    const bool pem = derOrPem.size() > sizeof kPem && std::search(derOrPem.begin(), derOrPem.end(), kPem,
                                                                  kPem + sizeof kPem - 1) != derOrPem.end();
    if (pem) {
        Ossl<BIO> bio = ossl::memBio(derOrPem);
        for (;;) {
            X509* x = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr);
            if (x == nullptr) {
                break;
            }
            out.emplace_back(x);
        }
        if (!out.empty()) {
            ERR_clear_error();  // конец текста — «no start line»
        }
        return out;
    }
    const unsigned char* p = derOrPem.data();
    X509* x = d2i_X509(nullptr, &p, static_cast<long>(derOrPem.size()));
    if (x != nullptr) {
        out.emplace_back(x);
    }
    return out;
}

}  // namespace ot::sign
