// Сведения о сертификатах X.509 для интерфейса и журнала (план 4.1, п. 3; эталон 3.8 — панель «Подписи»:
// кто, издатель, сроки, серийный номер): строки UTF-8 без экранирования кириллицы, время — ISO 8601 в UTC.
#pragma once

#include <openssl/x509.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/sign/ossl.h"

namespace ot::sign {

struct CertSummary {
    std::string subject;  // «CN=АкроБатя тест, OU=RSA, O=АкроБатя, C=RU» (RFC 2253, порядок от CN)
    std::string issuer;
    std::string commonName;  // CN субъекта (пусто — нет)
    std::string serial;      // HEX без ведущих нулей сверх байта: «2000»
    std::string notBefore;   // «2026-01-01T00:00:00Z»
    std::string notAfter;
    std::string keyAlgorithm;  // «RSA», «EC», «RSA-PSS»…
    int keyBits = 0;
    bool ca = false;
    Bytes der;
    Bytes sha256;  // отпечаток SHA-256 DER
};

std::string nameToString(const X509_NAME* name);
std::string commonName(const X509_NAME* name);
std::string serialHex(const ASN1_INTEGER* serial);
std::string timeToIso(const ASN1_TIME* time);
CertSummary summarize(X509* cert);

// Сертификат из DER или PEM (первый в тексте); ошибка — OsslError.
Ossl<X509> loadCertificate(std::span<const std::uint8_t> derOrPem);
// Все сертификаты PEM-текста или один DER.
std::vector<Ossl<X509>> loadCertificates(std::span<const std::uint8_t> derOrPem);

}  // namespace ot::sign
