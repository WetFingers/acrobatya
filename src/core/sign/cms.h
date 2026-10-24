// CMS SignedData для PAdES (эталон 3.8 «Механика», план 4.1, п. 3; sign-18 — CAdES-эквивалент
// ETSI.CAdES.detached): CmsBuilder — detached CAdES-B: CMS_sign(…CMS_PARTIAL|CMS_CADES|CMS_NO_SIGNING_TIME…)
// + CMS_add1_signer (ESS signing-certificate-v2 по хэшу подписи) + CMS_final по потоку или CMS_final_digest
// по готовому хэшу; signing-time и S/MIME capabilities не пишутся (время — /M словаря и метка TSA); метка
// времени B-T — unsigned-атрибут id-aa-timeStampToken после подписи. parseCms — сведения для интерфейса и
// проверок своим DER-разбором (структура и атрибуты не через OpenSSL CMS; сертификаты — OpenSSL). RSA-PSS —
// только чтение, ECDSA P-256/384 — как есть.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/sign/ossl.h"
#include "core/sign/x509_info.h"

namespace ot::sign {

struct SigningKey;

namespace oid {
inline constexpr std::string_view kData = "1.2.840.113549.1.7.1";
inline constexpr std::string_view kSignedData = "1.2.840.113549.1.7.2";
inline constexpr std::string_view kTstInfo = "1.2.840.113549.1.9.16.1.4";
inline constexpr std::string_view kContentType = "1.2.840.113549.1.9.3";
inline constexpr std::string_view kMessageDigest = "1.2.840.113549.1.9.4";
inline constexpr std::string_view kSigningTime = "1.2.840.113549.1.9.5";
inline constexpr std::string_view kSmimeCapabilities = "1.2.840.113549.1.9.15";
inline constexpr std::string_view kSigningCertificate = "1.2.840.113549.1.9.16.2.12";
inline constexpr std::string_view kSigningCertificateV2 = "1.2.840.113549.1.9.16.2.47";
inline constexpr std::string_view kTimeStampToken = "1.2.840.113549.1.9.16.2.14";
inline constexpr std::string_view kAdobeRevocation = "1.2.840.113583.1.1.8";
inline constexpr std::string_view kSha1 = "1.3.14.3.2.26";
inline constexpr std::string_view kSha256 = "2.16.840.1.101.3.4.2.1";
inline constexpr std::string_view kSha384 = "2.16.840.1.101.3.4.2.2";
inline constexpr std::string_view kSha512 = "2.16.840.1.101.3.4.2.3";
inline constexpr std::string_view kRsaEncryption = "1.2.840.113549.1.1.1";
inline constexpr std::string_view kRsaPss = "1.2.840.113549.1.1.10";
inline constexpr std::string_view kSha256WithRsa = "1.2.840.113549.1.1.11";
inline constexpr std::string_view kEcdsaWithSha256 = "1.2.840.10045.4.3.2";
inline constexpr std::string_view kEcdsaWithSha384 = "1.2.840.10045.4.3.3";
}  // namespace oid

// Читаемое имя алгоритма по OID: «SHA-256», «RSA», «ECDSA with SHA-256», «RSASSA-PSS»; неизвестный — сам OID.
std::string algorithmName(std::string_view oid);

enum class DigestAlgorithm { Sha256, Sha384, Sha512 };
const EVP_MD* evpDigest(DigestAlgorithm digest);
std::size_t digestSize(DigestAlgorithm digest);

class CmsBuilder {
public:
    // Ссылки на cert/key/chain берутся (up_ref), объекты вызывающего не освобождаются.
    CmsBuilder(X509* cert, EVP_PKEY* key, STACK_OF(X509) * chain,
               DigestAlgorithm digest = DigestAlgorithm::Sha256);
    explicit CmsBuilder(const SigningKey& id, DigestAlgorithm digest = DigestAlgorithm::Sha256);
    ~CmsBuilder();
    CmsBuilder(const CmsBuilder&) = delete;
    CmsBuilder& operator=(const CmsBuilder&) = delete;

    // Detached CAdES-B над потоком данных (BIO читается до конца). DER ContentInfo; ошибка — OsslError.
    Bytes build(BIO* data);
    // То же по готовому хэшу данных (длина — как у digest, иначе std::invalid_argument) — CMS_final_digest.
    Bytes buildFromDigest(std::span<const std::uint8_t> digest);

    // Значение подписи последнего build (imprint метки времени — хэш от него, RFC 3161 прил. A); до build —
    // std::logic_error.
    Bytes signatureValue() const;
    // Добавить unsigned-атрибут id-aa-timeStampToken (DER токена — ContentInfo целиком) к подписи последнего
    // build; новый DER. До build — std::logic_error.
    Bytes addUnsignedTimestamp(std::span<const std::uint8_t> tokenDer);
    // DER последнего build (с меткой, если добавлена).
    Bytes der() const;

private:
    Ossl<CMS_ContentInfo> start(CMS_SignerInfo** si);

    Ossl<X509> cert_;
    Ossl<EVP_PKEY> key_;
    Ossl<STACK_OF(X509)> chain_;
    DigestAlgorithm digest_;
    Ossl<CMS_ContentInfo> cms_;
};

// Те же операции над готовым DER (первый SignerInfo).
Bytes cmsSignatureValue(std::span<const std::uint8_t> cms);
Bytes cmsAddUnsignedTimestamp(std::span<const std::uint8_t> cms, std::span<const std::uint8_t> tokenDer);

// Целостность detached-подписи: CMS_verify(CMS_BINARY|CMS_DETACHED) по content (BIO читается до конца).
// store == nullptr — только целостность (CMS_NO_SIGNER_CERT_VERIFY без CMS_CADES, эталон 3.8 «Валидатор»);
// иначе ещё цепочка до store на текущее время с CMS_CADES (ESS signing-certificate сверяется). false —
// причина в error.
bool cmsVerifyDetached(std::span<const std::uint8_t> cms, BIO* content, X509_STORE* store,
                       std::string* error);

struct CmsAttribute {
    std::string oid;
    int values = 0;  // число значений в SET (CAdES: ровно одно)
    Bytes value;     // DER первого значения целиком
};

struct EssCertId {
    std::string hashAlgorithm;  // OID; у v2 без поля — SHA-256 по умолчанию (RFC 5035)
    Bytes certHash;
    bool hasIssuerSerial = false;
};

struct CmsInfo {
    int signedDataVersion = 0;
    std::string contentType;  // eContentType: у PAdES — id-data
    bool detached = true;     // eContent нет
    std::vector<std::string> digestAlgorithms;
    int signerCount = 0;

    // Первый SignerInfo.
    int signerVersion = 0;
    std::string signerIssuer;  // sid: IssuerAndSerialNumber (строкой, как CertSummary::issuer)
    std::string signerSerial;  // HEX
    Bytes signerKeyId;         // sid: SubjectKeyIdentifier (v3)
    std::string digestAlgorithm;
    std::string signatureAlgorithm;
    std::vector<CmsAttribute> signedAttributes;
    std::vector<CmsAttribute> unsignedAttributes;
    Bytes signedAttributesDer;  // SET OF Attribute с тегом SET — то, что подписано
    Bytes signature;

    Bytes messageDigest;                     // атрибут message-digest
    std::optional<std::string> signingTime;  // атрибут signing-time (ISO), у CAdES-B PAdES — нет
    std::optional<EssCertId> signingCertificateV2;
    bool hasSigningCertificateV1 = false;
    Bytes timestampToken;  // unsigned id-aa-timeStampToken (ContentInfo), пусто — нет

    std::vector<CertSummary> certificates;  // certificates SignedData в порядке файла
    int signerCertIndex = -1;               // сертификат подписанта по sid, -1 — нет в CMS

    const CmsAttribute* signedAttribute(std::string_view oid) const;
    const CmsAttribute* unsignedAttribute(std::string_view oid) const;
};

// Разбор DER (BER с неопределённой длиной — тоже). Не ContentInfo SignedData или испорчен —
// std::runtime_error.
CmsInfo parseCms(std::span<const std::uint8_t> der);

}  // namespace ot::sign
