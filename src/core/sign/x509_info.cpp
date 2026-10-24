#include "core/sign/x509_info.h"

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include <cstring>
#include <ctime>

#include "core/sign/chain.h"
#include "core/sign/identity.h"

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

namespace x509_details {

std::string objectText(const ASN1_OBJECT* o) {
    const int nid = OBJ_obj2nid(o);
    if (nid != NID_undef) {
        if (const char* sn = OBJ_nid2sn(nid)) {
            return sn;
        }
    }
    char buf[128] = {};
    OBJ_obj2txt(buf, sizeof buf, o, 1);
    return buf;
}

std::string extensionValue(X509_EXTENSION* ext) {
    Ossl<BIO> bio = ossl::newMemBio();
    if (X509V3_EXT_print(bio.get(), ext, 0, 0) != 1) {
        ERR_clear_error();
        const ASN1_OCTET_STRING* data = X509_EXTENSION_get_data(ext);
        return ossl::toHex(
            Bytes(ASN1_STRING_get0_data(data), ASN1_STRING_get0_data(data) + ASN1_STRING_length(data)));
    }
    const Bytes b = ossl::bioBytes(bio.get());
    return {b.begin(), b.end()};
}

}  // namespace x509_details

CertDetails certDetails(std::span<const std::uint8_t> der) {
    Ossl<X509> cert = loadCertificate(der);
    X509* x = cert.get();
    CertDetails d;
    d.version = static_cast<int>(X509_get_version(x)) + 1;
    d.signatureAlgorithm = OBJ_nid2ln(X509_get_signature_nid(x)) != nullptr
                               ? OBJ_nid2ln(X509_get_signature_nid(x))
                               : std::to_string(X509_get_signature_nid(x));
    if (EVP_PKEY* key = X509_get0_pubkey(x)) {
        const int id = EVP_PKEY_get_base_id(key);
        std::string name = id == EVP_PKEY_RSA ? "RSA" : id == EVP_PKEY_EC ? "EC" : OBJ_nid2sn(id);
        if (id == EVP_PKEY_EC) {
            char group[64] = {};
            std::size_t len = 0;
            if (EVP_PKEY_get_group_name(key, group, sizeof group, &len) == 1) {
                name += std::string(" ") + group;
            }
        } else {
            name += " " + std::to_string(EVP_PKEY_get_bits(key));
        }
        d.publicKey = name;
    }
    // Назначение ключа: биты в порядке RFC 5280.
    const uint32_t ku = X509_get_key_usage(x);
    if (ku != UINT32_MAX) {
        static const std::pair<uint32_t, const char*> kUsage[] = {{KU_DIGITAL_SIGNATURE, "digitalSignature"},
                                                                  {KU_NON_REPUDIATION, "nonRepudiation"},
                                                                  {KU_KEY_ENCIPHERMENT, "keyEncipherment"},
                                                                  {KU_DATA_ENCIPHERMENT, "dataEncipherment"},
                                                                  {KU_KEY_AGREEMENT, "keyAgreement"},
                                                                  {KU_KEY_CERT_SIGN, "keyCertSign"},
                                                                  {KU_CRL_SIGN, "cRLSign"},
                                                                  {KU_ENCIPHER_ONLY, "encipherOnly"},
                                                                  {KU_DECIPHER_ONLY, "decipherOnly"}};
        for (const auto& [bit, name] : kUsage) {
            if ((ku & bit) != 0) {
                d.keyUsage.emplace_back(name);
            }
        }
    }
    int critical = 0;
    if (auto* usage = static_cast<ASN1_BIT_STRING*>(X509_get_ext_d2i(x, NID_key_usage, &critical, nullptr))) {
        d.keyUsageCritical = critical == 1;
        ASN1_BIT_STRING_free(usage);
    }
    if (auto* eku =
            static_cast<EXTENDED_KEY_USAGE*>(X509_get_ext_d2i(x, NID_ext_key_usage, nullptr, nullptr))) {
        for (int i = 0; i < sk_ASN1_OBJECT_num(eku); ++i) {
            const ASN1_OBJECT* o = sk_ASN1_OBJECT_value(eku, i);
            const int nid = OBJ_obj2nid(o);
            // Неизвестные OpenSSL (documentSigning, Adobe) — OID.
            if (nid == NID_undef) {
                char buf[128] = {};
                OBJ_obj2txt(buf, sizeof buf, o, 1);
                d.extendedKeyUsage.emplace_back(buf);
            } else {
                d.extendedKeyUsage.emplace_back(OBJ_nid2sn(nid));
            }
        }
        sk_ASN1_OBJECT_pop_free(eku, ASN1_OBJECT_free);
    }
    if (auto* pol = static_cast<CERTIFICATEPOLICIES*>(
            X509_get_ext_d2i(x, NID_certificate_policies, nullptr, nullptr))) {
        for (int i = 0; i < sk_POLICYINFO_num(pol); ++i) {
            char buf[128] = {};
            OBJ_obj2txt(buf, sizeof buf, sk_POLICYINFO_value(pol, i)->policyid, 1);
            d.policies.emplace_back(buf);
        }
        CERTIFICATEPOLICIES_free(pol);
    }
    ERR_clear_error();
    d.ocspUrls = ocspUrls(x);
    d.crlUrls = crlUrls(x);
    for (int i = 0; i < X509_get_ext_count(x); ++i) {
        X509_EXTENSION* ext = X509_get_ext(x, i);
        std::string name = x509_details::objectText(X509_EXTENSION_get_object(ext));
        if (X509_EXTENSION_get_critical(ext) != 0) {
            name += " (critical)";
        }
        d.extensions.emplace_back(std::move(name), x509_details::extensionValue(ext));
    }
    d.sha1 = certificateThumbprint(x);
    d.sha256 = certificateId(x);
    d.selfSigned = X509_check_issued(x, x) == X509_V_OK;
    ERR_clear_error();
    return d;
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
