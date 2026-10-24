// CMS SignedData с внешней подписью (эталон 3.8 «CNG-путь», 11 риск 2; план 4.3, п. 2; sign-08, sign-06 —
// ключ на устройстве): закрытый ключ вне OpenSSL (CNG, смарт-карта), CMS_add1_signer без ключа невозможен —
// поэтому PKCS7-API. Signed-атрибуты CAdES-B собираются своим кодом в DER (der.h): content-type id-data,
// message-digest, ESS signing-certificate-v2 (ESSCertIDv2 с issuerSerial; хэш сертификата — алгоритмом
// подписи, у SHA-256 поля hashAlgorithm нет), без signing-time и S/MIME capabilities — байт в байт как у
// CmsBuilder (OpenSSL CMS_add1_signer + CMS_CADES | CMS_NO_SIGNING_TIME). Хэш SET атрибутов → внешний
// подписант → значение подписи — в PKCS7_SIGNER_INFO enc_digest (ASN1_STRING_set). Остальное — как у
// CMS-пути, один DER-формат: digestAlgorithm без параметров, RSA — rsaEncryption с NULL, ECDSA —
// ecdsa-with-SHA*, сертификаты — DER-порядок SET OF, версии 1; RSA PKCS#1 v1.5 детерминирован — DER совпадает
// с CmsBuilder целиком. Проверка — openssl cms -verify -cades и cmsVerifyDetached; метка времени —
// cmsAddUnsignedTimestamp.
#pragma once

#include <cstdint>
#include <functional>
#include <span>

#include "core/sign/cms.h"
#include "core/sign/ossl.h"

namespace ot::sign {

// Хэш signed-атрибутов → значение подписи в формате CMS: RSA PKCS#1 v1.5 — как есть, ECDSA — DER
// ECDSA-Sig-Value. Ошибка — исключение подписанта (CngError…), пустой результат — OsslError.
using ExternalSignFn = std::function<Bytes(std::span<const std::uint8_t> hash)>;

// ESS SigningCertificateV2 сертификата (значение атрибута, DER SEQUENCE).
Bytes essSigningCertificateV2(X509* cert, DigestAlgorithm digest);
// Signed-атрибуты CAdES-B по хэшу данных — DER SET OF Attribute (тег 0x31): то, что подписывается.
Bytes cadesSignedAttributes(X509* cert, DigestAlgorithm digest, std::span<const std::uint8_t> contentDigest);

class ExternalCmsBuilder {
public:
    // Ссылки на cert/chain берутся (up_ref); chain — остальные сертификаты (без подписанта), может быть
    // nullptr.
    ExternalCmsBuilder(X509* cert, STACK_OF(X509) * chain, ExternalSignFn sign,
                       DigestAlgorithm digest = DigestAlgorithm::Sha256);
    ~ExternalCmsBuilder();
    ExternalCmsBuilder(const ExternalCmsBuilder&) = delete;
    ExternalCmsBuilder& operator=(const ExternalCmsBuilder&) = delete;

    // Detached CAdES-B по готовому хэшу данных (длина — как у digest, иначе std::invalid_argument). DER
    // ContentInfo; ошибка OpenSSL — OsslError, подписанта — его исключение.
    Bytes buildFromDigest(std::span<const std::uint8_t> digest);
    // Signed-атрибуты последнего build (DER SET); до build — пусто.
    const Bytes& signedAttributes() const noexcept { return attributes_; }
    // Значение подписи последнего build; до build — std::logic_error.
    Bytes signatureValue() const;
    Bytes der() const;  // DER последнего build; до build — std::logic_error

private:
    Ossl<X509> cert_;
    Ossl<STACK_OF(X509)> chain_;
    ExternalSignFn sign_;
    DigestAlgorithm digest_;
    Bytes attributes_;
    Bytes signature_;
    Bytes der_;
};

}  // namespace ot::sign
