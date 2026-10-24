#include "core/sign/cms_external.h"

#include <openssl/asn1t.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pkcs7.h>
#include <openssl/x509.h>

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

#include "core/sign/der.h"

namespace ot::sign {

namespace {

using der::Encoded;

std::string_view digestOid(DigestAlgorithm digest) {
    switch (digest) {
        case DigestAlgorithm::Sha384:
            return oid::kSha384;
        case DigestAlgorithm::Sha512:
            return oid::kSha512;
        case DigestAlgorithm::Sha256:
        default:
            return oid::kSha256;
    }
}

// Attribute ::= SEQUENCE { attrType OID, attrValues SET OF (одно значение) }.
Encoded attribute(std::string_view type, std::span<const std::uint8_t> value) {
    const Encoded o = der::encodeOid(type);
    const Encoded values = der::encode(der::kSet, value);
    return der::encode(der::kSequence, der::join({o, values}));
}

Encoded x509Der(X509* cert) { return ossl::toDer(cert, i2d_X509, "i2d_X509"); }

// Алгоритмы — как у CMS-пути (X509_ALGOR_set_md: у SHA-2 без параметров).
void setDigestAlgorithm(X509_ALGOR* alg, DigestAlgorithm digest) {
    X509_ALGOR_set_md(alg, evpDigest(digest));
}

}  // namespace

Bytes essSigningCertificateV2(X509* cert, DigestAlgorithm digest) {
    if (cert == nullptr) {
        throw std::invalid_argument("essSigningCertificateV2: no certificate");
    }
    // ESSCertIDv2 ::= SEQUENCE { hashAlgorithm DEFAULT sha256, certHash OCTET STRING, issuerSerial
    // IssuerSerial } IssuerSerial ::= SEQUENCE { issuer GeneralNames (directoryName [4] EXPLICIT Name),
    // serialNumber INTEGER }
    Bytes hash(EVP_MAX_MD_SIZE);
    unsigned int hashLen = 0;
    ossl::check(X509_digest(cert, evpDigest(digest), hash.data(), &hashLen), "X509_digest");
    hash.resize(hashLen);
    const Encoded issuer = ossl::toDer(X509_get_issuer_name(cert), i2d_X509_NAME, "i2d_X509_NAME");
    const Encoded serial = ossl::toDer(X509_get0_serialNumber(cert), i2d_ASN1_INTEGER, "i2d_ASN1_INTEGER");
    const Encoded generalNames = der::encode(der::kSequence, der::encode(der::context(4), issuer));
    const Encoded issuerSerial = der::encode(der::kSequence, der::join({generalNames, serial}));
    Encoded certId;
    const Encoded certHash = der::encode(der::kOctetString, hash);
    if (digest == DigestAlgorithm::Sha256) {
        certId = der::encode(der::kSequence, der::join({certHash, issuerSerial}));
    } else {
        const Encoded alg = der::encode(der::kSequence, der::encodeOid(digestOid(digest)));
        certId = der::encode(der::kSequence, der::join({alg, certHash, issuerSerial}));
    }
    const Encoded certs = der::encode(der::kSequence, certId);
    return der::encode(der::kSequence, certs);
}

Bytes cadesSignedAttributes(X509* cert, DigestAlgorithm digest, std::span<const std::uint8_t> contentDigest) {
    if (contentDigest.size() != digestSize(digest)) {
        throw std::invalid_argument("cadesSignedAttributes: digest length " +
                                    std::to_string(contentDigest.size()) + ", expected " +
                                    std::to_string(digestSize(digest)));
    }
    std::vector<Encoded> attrs;
    attrs.push_back(attribute(oid::kContentType, der::encodeOid(oid::kData)));
    attrs.push_back(attribute(oid::kMessageDigest, der::encode(der::kOctetString, contentDigest)));
    attrs.push_back(attribute(oid::kSigningCertificateV2, essSigningCertificateV2(cert, digest)));
    return der::encodeSetOf(std::move(attrs));
}

ExternalCmsBuilder::ExternalCmsBuilder(X509* cert, STACK_OF(X509) * chain, ExternalSignFn sign,
                                       DigestAlgorithm digest)
    : sign_(std::move(sign)), digest_(digest) {
    if (cert == nullptr || !sign_) {
        throw std::invalid_argument("ExternalCmsBuilder: certificate and signer are required");
    }
    X509_up_ref(cert);
    cert_.reset(cert);
    chain_.reset(chain != nullptr ? X509_chain_up_ref(chain) : sk_X509_new_null());
    if (!chain_) {
        throw OsslError::fromQueue("X509_chain_up_ref");
    }
}

ExternalCmsBuilder::~ExternalCmsBuilder() = default;

Bytes ExternalCmsBuilder::buildFromDigest(std::span<const std::uint8_t> digest) {
    if (digest.size() != digestSize(digest_)) {
        throw std::invalid_argument("ExternalCmsBuilder::buildFromDigest: digest length " +
                                    std::to_string(digest.size()) + ", expected " +
                                    std::to_string(digestSize(digest_)));
    }
    ERR_clear_error();
    const Bytes attrs = cadesSignedAttributes(cert_.get(), digest_, digest);

    // Подпись над DER SET атрибутов — внешним подписантом.
    Bytes hash(EVP_MAX_MD_SIZE);
    unsigned int hashLen = 0;
    ossl::check(EVP_Digest(attrs.data(), attrs.size(), hash.data(), &hashLen, evpDigest(digest_), nullptr),
                "EVP_Digest");
    hash.resize(hashLen);
    Bytes signature = sign_(hash);
    if (signature.empty()) {
        throw OsslError(0, "external signer returned an empty signature");
    }

    // SignedData: id-data без eContent (detached), версии 1, один SignerInfo по issuerAndSerialNumber.
    Ossl<PKCS7> p7(ossl::checkPtr(PKCS7_new(), "PKCS7_new"));
    ossl::check(PKCS7_set_type(p7.get(), NID_pkcs7_signed), "PKCS7_set_type");
    ossl::check(PKCS7_content_new(p7.get(), NID_pkcs7_data), "PKCS7_content_new");
    PKCS7_set_detached(p7.get(), 1);
    // Открытого ключа сертификата хватает PKCS7_SIGNER_INFO_set: он выбирает только алгоритм подписи
    // (rsaEncryption с NULL, ecdsa-with-SHA*), подписывать им нечего.
    PKCS7_SIGNER_INFO* si = ossl::checkPtr(
        PKCS7_add_signature(p7.get(), cert_.get(), X509_get0_pubkey(cert_.get()), evpDigest(digest_)),
        "PKCS7_add_signature");
    setDigestAlgorithm(si->digest_alg, digest_);
    STACK_OF(X509_ALGOR)* mdAlgs = p7->d.sign->md_algs;
    for (int i = 0; i < sk_X509_ALGOR_num(mdAlgs); ++i) {
        setDigestAlgorithm(sk_X509_ALGOR_value(mdAlgs, i), digest_);
    }

    // Атрибуты — наш DER как есть (PKCS7_ATTR_VERIFY — SEQUENCE OF с тегом SET: порядок сохраняется).
    const unsigned char* p = attrs.data();
    auto* decoded = reinterpret_cast<STACK_OF(X509_ATTRIBUTE)*>(
        ASN1_item_d2i(nullptr, &p, static_cast<long>(attrs.size()), ASN1_ITEM_rptr(PKCS7_ATTR_VERIFY)));
    if (decoded == nullptr) {
        throw OsslError::fromQueue("ASN1_item_d2i(signed attributes)");
    }
    sk_X509_ATTRIBUTE_pop_free(si->auth_attr, X509_ATTRIBUTE_free);
    si->auth_attr = decoded;
    ossl::check(ASN1_STRING_set(si->enc_digest, signature.data(), static_cast<int>(signature.size())),
                "ASN1_STRING_set");

    // Сертификаты: подписант и цепочка без повторов, в DER-порядке SET OF (как пишет CMS-путь).
    std::vector<Encoded> certs;
    certs.push_back(x509Der(cert_.get()));
    for (int i = 0; i < sk_X509_num(chain_.get()); ++i) {
        Encoded c = x509Der(sk_X509_value(chain_.get(), i));
        if (std::find(certs.begin(), certs.end(), c) == certs.end()) {
            certs.push_back(std::move(c));
        }
    }
    std::sort(certs.begin(), certs.end(),
              [](const Encoded& a, const Encoded& b) { return std::ranges::lexicographical_compare(a, b); });
    for (const Encoded& c : certs) {
        const unsigned char* q = c.data();
        Ossl<X509> x(ossl::checkPtr(d2i_X509(nullptr, &q, static_cast<long>(c.size())), "d2i_X509"));
        ossl::check(PKCS7_add_certificate(p7.get(), x.get()), "PKCS7_add_certificate");
    }

    der_ = ossl::toDer(p7.get(), i2d_PKCS7, "i2d_PKCS7");
    attributes_ = attrs;
    signature_ = std::move(signature);
    return der_;
}

Bytes ExternalCmsBuilder::signatureValue() const {
    if (der_.empty()) {
        throw std::logic_error("ExternalCmsBuilder: nothing built yet");
    }
    return signature_;
}

Bytes ExternalCmsBuilder::der() const {
    if (der_.empty()) {
        throw std::logic_error("ExternalCmsBuilder: nothing built yet");
    }
    return der_;
}

}  // namespace ot::sign
