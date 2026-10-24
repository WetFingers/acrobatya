#include "core/sign/cms.h"

#include <openssl/cms.h>
#include <openssl/err.h>

#include <algorithm>
#include <stdexcept>

#include "core/sign/der.h"
#include "core/sign/pkcs12.h"

namespace ot::sign {

namespace {

// CAdES-B для PAdES (эталон 3.8): detached, двоичные данные, подписант добавляется отдельно, без S/MIME
// capabilities и signing-time, с ESS signing-certificate(-v2).
constexpr unsigned int kCadesFlags =
    CMS_DETACHED | CMS_BINARY | CMS_PARTIAL | CMS_NOSMIMECAP | CMS_CADES | CMS_NO_SIGNING_TIME;
constexpr unsigned int kFinalFlags = CMS_DETACHED | CMS_BINARY;

Ossl<CMS_ContentInfo> cmsFromDer(std::span<const std::uint8_t> der) {
    const unsigned char* p = der.data();
    return Ossl<CMS_ContentInfo>(ossl::checkPtr(
        d2i_CMS_ContentInfo(nullptr, &p, static_cast<long>(der.size())), "d2i_CMS_ContentInfo"));
}

CMS_SignerInfo* firstSigner(CMS_ContentInfo* cms) {
    STACK_OF(CMS_SignerInfo)* signers = CMS_get0_SignerInfos(cms);
    if (signers == nullptr || sk_CMS_SignerInfo_num(signers) < 1) {
        throw std::runtime_error("CMS: no SignerInfo");
    }
    return sk_CMS_SignerInfo_value(signers, 0);
}

Bytes signatureOf(CMS_ContentInfo* cms) {
    const ASN1_OCTET_STRING* sig = CMS_SignerInfo_get0_signature(firstSigner(cms));
    if (sig == nullptr) {
        throw std::runtime_error("CMS: no signature value");
    }
    const unsigned char* d = ASN1_STRING_get0_data(sig);
    return {d, d + ASN1_STRING_length(sig)};
}

void addTimestampAttribute(CMS_ContentInfo* cms, std::span<const std::uint8_t> token) {
    if (token.empty()) {
        throw std::invalid_argument("empty time-stamp token");
    }
    Ossl<ASN1_OBJECT> obj(ossl::checkPtr(OBJ_txt2obj(oid::kTimeStampToken.data(), 1), "OBJ_txt2obj"));
    // V_ASN1_SEQUENCE: значение — DER токена целиком (ContentInfo).
    ossl::check(CMS_unsigned_add1_attr_by_OBJ(firstSigner(cms), obj.get(), V_ASN1_SEQUENCE, token.data(),
                                              static_cast<int>(token.size())),
                "CMS_unsigned_add1_attr_by_OBJ");
}

Bytes cmsDer(CMS_ContentInfo* cms) { return ossl::toDer(cms, i2d_CMS_ContentInfo, "i2d_CMS_ContentInfo"); }

// --- свой разбор
// ---------------------------------------------------------------------------------------------

std::string algorithmOid(const der::Node& algorithmIdentifier) {
    der::Reader r(algorithmIdentifier.value);
    return der::toOid(r.expect(der::kOid, "AlgorithmIdentifier.algorithm").value);
}

std::vector<CmsAttribute> parseAttributes(std::span<const std::uint8_t> setContent) {
    std::vector<CmsAttribute> out;
    der::Reader r(setContent);
    while (!r.atEnd()) {
        const der::Node attr = r.expect(der::kSequence, "Attribute");
        der::Reader a(attr.value);
        CmsAttribute item;
        item.oid = der::toOid(a.expect(der::kOid, "Attribute.attrType").value);
        const der::Node values = a.expect(der::kSet, "Attribute.attrValues");
        der::Reader v(values.value);
        while (!v.atEnd()) {
            const der::Node value = v.next();
            if (item.values == 0) {
                item.value.assign(value.encoded.begin(), value.encoded.end());
            }
            ++item.values;
        }
        out.push_back(std::move(item));
    }
    return out;
}

std::optional<EssCertId> parseEss(const Bytes& value, bool v2) {
    der::Reader top(value);
    const der::Node sc = top.expect(der::kSequence, "SigningCertificate");
    der::Reader in(sc.value);
    const der::Node certs = in.expect(der::kSequence, "SigningCertificate.certs");
    der::Reader list(certs.value);
    if (list.atEnd()) {
        return std::nullopt;
    }
    const der::Node id = list.expect(der::kSequence, "ESSCertID");
    der::Reader f(id.value);
    EssCertId out;
    out.hashAlgorithm = v2 ? std::string(oid::kSha256) : std::string(oid::kSha1);
    der::Node alg;
    if (v2 && f.nextIf(der::kSequence, alg)) {
        out.hashAlgorithm = algorithmOid(alg);
    }
    const der::Node hash = f.expect(der::kOctetString, "ESSCertID.certHash");
    out.certHash.assign(hash.value.begin(), hash.value.end());
    out.hasIssuerSerial = f.peekTag() == der::kSequence;
    return out;
}

// issuer и serialNumber из DER сертификата (TBSCertificate).
bool certIssuerSerial(std::span<const std::uint8_t> cert, std::span<const std::uint8_t>& issuer,
                      std::span<const std::uint8_t>& serial) {
    try {
        der::Reader top(cert);
        der::Reader c(top.expect(der::kSequence, "Certificate").value);
        der::Reader tbs(c.expect(der::kSequence, "TBSCertificate").value);
        der::Node version;
        tbs.nextIf(der::context(0), version);
        serial = tbs.expect(der::kInteger, "serialNumber").value;
        (void)tbs.expect(der::kSequence, "signature");
        issuer = tbs.expect(der::kSequence, "issuer").encoded;
        return true;
    } catch (const der::DerError&) {
        return false;
    }
}

std::string nameFromDer(std::span<const std::uint8_t> nameDer) {
    const unsigned char* p = nameDer.data();
    Ossl<X509_NAME> name(d2i_X509_NAME(nullptr, &p, static_cast<long>(nameDer.size())));
    if (!name) {
        ERR_clear_error();
        return {};
    }
    return nameToString(name.get());
}

std::string serialFromDer(std::span<const std::uint8_t> integerValue) {
    // Как у CertSummary: HEX значения без лишних ведущих нулей.
    std::size_t i = 0;
    while (i + 1 < integerValue.size() && integerValue[i] == 0) {
        ++i;
    }
    std::string hex = ossl::toHex(integerValue.subspan(i));
    return hex.empty() ? "0" : hex;
}

}  // namespace

std::string algorithmName(std::string_view o) {
    static constexpr std::pair<std::string_view, std::string_view> kNames[] = {
        {oid::kSha1, "SHA-1"},
        {oid::kSha256, "SHA-256"},
        {oid::kSha384, "SHA-384"},
        {oid::kSha512, "SHA-512"},
        {oid::kRsaEncryption, "RSA"},
        {oid::kRsaPss, "RSASSA-PSS"},
        {oid::kSha256WithRsa, "RSA with SHA-256"},
        {"1.2.840.113549.1.1.5", "RSA with SHA-1"},
        {"1.2.840.113549.1.1.12", "RSA with SHA-384"},
        {"1.2.840.113549.1.1.13", "RSA with SHA-512"},
        {"1.2.840.10045.2.1", "ECDSA"},
        {"1.2.840.10045.4.1", "ECDSA with SHA-1"},
        {oid::kEcdsaWithSha256, "ECDSA with SHA-256"},
        {oid::kEcdsaWithSha384, "ECDSA with SHA-384"},
        {"1.2.840.10045.4.3.4", "ECDSA with SHA-512"},
        {"1.3.101.112", "Ed25519"},
    };
    for (const auto& [k, v] : kNames) {
        if (k == o) {
            return std::string(v);
        }
    }
    return std::string(o);
}

const EVP_MD* evpDigest(DigestAlgorithm digest) {
    switch (digest) {
        case DigestAlgorithm::Sha384:
            return EVP_sha384();
        case DigestAlgorithm::Sha512:
            return EVP_sha512();
        case DigestAlgorithm::Sha256:
        default:
            return EVP_sha256();
    }
}

std::size_t digestSize(DigestAlgorithm digest) {
    return static_cast<std::size_t>(EVP_MD_get_size(evpDigest(digest)));
}

CmsBuilder::CmsBuilder(X509* cert, EVP_PKEY* key, STACK_OF(X509) * chain, DigestAlgorithm digest)
    : digest_(digest) {
    if (cert == nullptr || key == nullptr) {
        throw std::invalid_argument("CmsBuilder: certificate and key are required");
    }
    X509_up_ref(cert);
    cert_.reset(cert);
    EVP_PKEY_up_ref(key);
    key_.reset(key);
    chain_.reset(chain != nullptr ? X509_chain_up_ref(chain) : sk_X509_new_null());
    if (!chain_) {
        throw OsslError::fromQueue("X509_chain_up_ref");
    }
}

CmsBuilder::CmsBuilder(const SigningKey& id, DigestAlgorithm digest)
    : CmsBuilder(id.cert.get(), id.key.get(), id.chain.get(), digest) {}

CmsBuilder::~CmsBuilder() = default;

Ossl<CMS_ContentInfo> CmsBuilder::start(CMS_SignerInfo** si) {
    ERR_clear_error();
    Ossl<CMS_ContentInfo> cms(
        ossl::checkPtr(CMS_sign(nullptr, nullptr, chain_.get(), nullptr, kCadesFlags), "CMS_sign"));
    *si = ossl::checkPtr(CMS_add1_signer(cms.get(), cert_.get(), key_.get(), evpDigest(digest_), kCadesFlags),
                         "CMS_add1_signer");
    return cms;
}

Bytes CmsBuilder::build(BIO* data) {
    if (data == nullptr) {
        throw std::invalid_argument("CmsBuilder::build: no data");
    }
    CMS_SignerInfo* si = nullptr;
    Ossl<CMS_ContentInfo> cms = start(&si);
    ossl::check(CMS_final(cms.get(), data, nullptr, kFinalFlags), "CMS_final");
    cms_ = std::move(cms);
    return der();
}

Bytes CmsBuilder::buildFromDigest(std::span<const std::uint8_t> digest) {
    if (digest.size() != digestSize(digest_)) {
        throw std::invalid_argument("CmsBuilder::buildFromDigest: digest length " +
                                    std::to_string(digest.size()) + ", expected " +
                                    std::to_string(digestSize(digest_)));
    }
    CMS_SignerInfo* si = nullptr;
    Ossl<CMS_ContentInfo> cms = start(&si);
    ossl::check(CMS_final_digest(cms.get(), digest.data(), static_cast<unsigned int>(digest.size()), nullptr,
                                 kFinalFlags),
                "CMS_final_digest");
    cms_ = std::move(cms);
    return der();
}

Bytes CmsBuilder::signatureValue() const {
    if (!cms_) {
        throw std::logic_error("CmsBuilder: nothing built yet");
    }
    return signatureOf(cms_.get());
}

Bytes CmsBuilder::addUnsignedTimestamp(std::span<const std::uint8_t> tokenDer) {
    if (!cms_) {
        throw std::logic_error("CmsBuilder: time stamp before signature");
    }
    addTimestampAttribute(cms_.get(), tokenDer);
    return der();
}

Bytes CmsBuilder::der() const {
    if (!cms_) {
        throw std::logic_error("CmsBuilder: nothing built yet");
    }
    return cmsDer(cms_.get());
}

Bytes cmsSignatureValue(std::span<const std::uint8_t> cms) {
    Ossl<CMS_ContentInfo> ci = cmsFromDer(cms);
    return signatureOf(ci.get());
}

Bytes cmsAddUnsignedTimestamp(std::span<const std::uint8_t> cms, std::span<const std::uint8_t> tokenDer) {
    Ossl<CMS_ContentInfo> ci = cmsFromDer(cms);
    addTimestampAttribute(ci.get(), tokenDer);
    return cmsDer(ci.get());
}

bool cmsVerifyDetached(std::span<const std::uint8_t> cms, BIO* content, X509_STORE* store,
                       std::string* error) {
    ERR_clear_error();
    Ossl<CMS_ContentInfo> ci;
    try {
        ci = cmsFromDer(cms);
    } catch (const std::exception& e) {
        if (error != nullptr) {
            *error = e.what();
        }
        return false;
    }
    unsigned int flags = CMS_BINARY | CMS_DETACHED;
    flags |= store == nullptr ? CMS_NO_SIGNER_CERT_VERIFY : CMS_CADES;
    if (CMS_verify(ci.get(), nullptr, store, content, nullptr, flags) != 1) {
        if (error != nullptr) {
            *error = OsslError::fromQueue("CMS_verify").what();
        } else {
            ERR_clear_error();
        }
        return false;
    }
    return true;
}

const CmsAttribute* CmsInfo::signedAttribute(std::string_view o) const {
    const auto it = std::find_if(signedAttributes.begin(), signedAttributes.end(),
                                 [&](const auto& a) { return a.oid == o; });
    return it == signedAttributes.end() ? nullptr : &*it;
}

const CmsAttribute* CmsInfo::unsignedAttribute(std::string_view o) const {
    const auto it = std::find_if(unsignedAttributes.begin(), unsignedAttributes.end(),
                                 [&](const auto& a) { return a.oid == o; });
    return it == unsignedAttributes.end() ? nullptr : &*it;
}

CmsInfo parseCms(std::span<const std::uint8_t> bytes) {
    CmsInfo info;
    der::Reader top(bytes);
    der::Reader ci(top.expect(der::kSequence, "ContentInfo").value);
    if (der::toOid(ci.expect(der::kOid, "ContentInfo.contentType").value) != oid::kSignedData) {
        throw std::runtime_error("CMS: not SignedData");
    }
    der::Reader explicitContent(ci.expect(der::context(0), "ContentInfo.content").value);
    der::Reader sd(explicitContent.expect(der::kSequence, "SignedData").value);
    info.signedDataVersion = static_cast<int>(der::toInt(sd.expect(der::kInteger, "SignedData.version")));
    {
        der::Reader algs(sd.expect(der::kSet, "SignedData.digestAlgorithms").value);
        while (!algs.atEnd()) {
            info.digestAlgorithms.push_back(
                algorithmOid(algs.expect(der::kSequence, "DigestAlgorithmIdentifier")));
        }
    }
    {
        der::Reader eci(sd.expect(der::kSequence, "EncapsulatedContentInfo").value);
        info.contentType = der::toOid(eci.expect(der::kOid, "eContentType").value);
        der::Node eContent;
        info.detached = !eci.nextIf(der::context(0), eContent);
    }
    der::Node certs;
    if (sd.nextIf(der::context(0), certs)) {
        der::Reader list(certs.value);
        while (!list.atEnd()) {
            const der::Node c = list.next();
            if (c.tag != der::kSequence) {
                continue;  // атрибутные и прочие сертификаты — мимо
            }
            const unsigned char* p = c.encoded.data();
            Ossl<X509> x(d2i_X509(nullptr, &p, static_cast<long>(c.encoded.size())));
            if (!x) {
                ERR_clear_error();
                continue;
            }
            info.certificates.push_back(summarize(x.get()));
        }
    }
    der::Node crls;
    sd.nextIf(der::context(1), crls);
    der::Reader signers(sd.expect(der::kSet, "SignedData.signerInfos").value);
    bool first = true;
    while (!signers.atEnd()) {
        const der::Node signerNode = signers.expect(der::kSequence, "SignerInfo");
        ++info.signerCount;
        if (!first) {
            continue;
        }
        first = false;
        der::Reader si(signerNode.value);
        info.signerVersion = static_cast<int>(der::toInt(si.expect(der::kInteger, "SignerInfo.version")));
        std::span<const std::uint8_t> sidIssuer;
        std::span<const std::uint8_t> sidSerial;
        der::Node sid;
        if (si.nextIf(der::kSequence, sid)) {
            der::Reader ias(sid.value);
            sidIssuer = ias.expect(der::kSequence, "IssuerAndSerialNumber.issuer").encoded;
            sidSerial = ias.expect(der::kInteger, "IssuerAndSerialNumber.serialNumber").value;
            info.signerIssuer = nameFromDer(sidIssuer);
            info.signerSerial = serialFromDer(sidSerial);
        } else {
            const der::Node ski = si.expect(der::context(0, false), "SignerIdentifier.subjectKeyIdentifier");
            info.signerKeyId.assign(ski.value.begin(), ski.value.end());
        }
        info.digestAlgorithm = algorithmOid(si.expect(der::kSequence, "SignerInfo.digestAlgorithm"));
        der::Node signedAttrs;
        if (si.nextIf(der::context(0), signedAttrs)) {
            info.signedAttributes = parseAttributes(signedAttrs.value);
            info.signedAttributesDer.assign(signedAttrs.encoded.begin(), signedAttrs.encoded.end());
            info.signedAttributesDer[0] = der::kSet;
        }
        info.signatureAlgorithm = algorithmOid(si.expect(der::kSequence, "SignerInfo.signatureAlgorithm"));
        const der::Node sig = si.expect(der::kOctetString, "SignerInfo.signature");
        info.signature.assign(sig.value.begin(), sig.value.end());
        der::Node unsignedAttrs;
        if (si.nextIf(der::context(1), unsignedAttrs)) {
            info.unsignedAttributes = parseAttributes(unsignedAttrs.value);
        }

        if (const CmsAttribute* md = info.signedAttribute(oid::kMessageDigest)) {
            der::Reader r(md->value);
            const der::Node v = r.expect(der::kOctetString, "message-digest");
            info.messageDigest.assign(v.value.begin(), v.value.end());
        }
        if (const CmsAttribute* st = info.signedAttribute(oid::kSigningTime)) {
            der::Reader r(st->value);
            const der::Node v = r.next();
            info.signingTime = der::toTime(v.tag, v.value);
        }
        if (const CmsAttribute* ess = info.signedAttribute(oid::kSigningCertificateV2)) {
            info.signingCertificateV2 = parseEss(ess->value, true);
        }
        info.hasSigningCertificateV1 = info.signedAttribute(oid::kSigningCertificate) != nullptr;
        if (const CmsAttribute* ts = info.unsignedAttribute(oid::kTimeStampToken)) {
            info.timestampToken = ts->value;
        }

        // Сертификат подписанта по sid.
        for (std::size_t i = 0; i < info.certificates.size(); ++i) {
            const CertSummary& c = info.certificates[i];
            bool match = false;
            if (!sidIssuer.empty()) {
                std::span<const std::uint8_t> issuer;
                std::span<const std::uint8_t> serial;
                match = certIssuerSerial(c.der, issuer, serial) && std::ranges::equal(issuer, sidIssuer) &&
                        std::ranges::equal(serial, sidSerial);
            } else if (!info.signerKeyId.empty()) {
                const unsigned char* p = c.der.data();
                Ossl<X509> x(d2i_X509(nullptr, &p, static_cast<long>(c.der.size())));
                const ASN1_OCTET_STRING* skid = x ? X509_get0_subject_key_id(x.get()) : nullptr;
                match =
                    skid != nullptr &&
                    static_cast<std::size_t>(ASN1_STRING_length(skid)) == info.signerKeyId.size() &&
                    std::equal(info.signerKeyId.begin(), info.signerKeyId.end(), ASN1_STRING_get0_data(skid));
                ERR_clear_error();
            }
            if (match) {
                info.signerCertIndex = static_cast<int>(i);
                break;
            }
        }
    }
    if (info.signerCount == 0) {
        throw std::runtime_error("CMS: no SignerInfo");
    }
    return info;
}

}  // namespace ot::sign
