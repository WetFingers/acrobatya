// OpenSSL в ядре подписей (эталон 3.8, 5.4; план 4.1): RAII Ossl<T> с освобождением по типу, ошибки OsslError
// с разобранной очередью ERR, BIO в памяти и по диапазонам файла (/ByteRange подписи: части вокруг /Contents
// читаются потоком, без загрузки файла), версия ≥ 3.5 — CMS_NO_SIGNING_TIME появился только в 3.5.0.
// Объекты OpenSSL не покидают core/sign: наружу — Bytes, строки и свои структуры.
#pragma once

#include <openssl/bio.h>
#include <openssl/cms.h>
#include <openssl/evp.h>
#include <openssl/ocsp.h>
#include <openssl/pkcs12.h>
#include <openssl/ts.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ot::sign {

using Bytes = std::vector<std::uint8_t>;

// Освобождение объекта OpenSSL по типу (для Ossl<T>).
template <typename T>
struct OsslFree;

#define OT_OSSL_FREE(T, fn)                             \
    template <>                                         \
    struct OsslFree<T> {                                \
        void operator()(T* p) const noexcept { fn(p); } \
    }
OT_OSSL_FREE(BIO, BIO_free_all);
OT_OSSL_FREE(EVP_PKEY, EVP_PKEY_free);
OT_OSSL_FREE(EVP_PKEY_CTX, EVP_PKEY_CTX_free);
OT_OSSL_FREE(EVP_MD_CTX, EVP_MD_CTX_free);
OT_OSSL_FREE(X509, X509_free);
OT_OSSL_FREE(X509_CRL, X509_CRL_free);
OT_OSSL_FREE(X509_STORE, X509_STORE_free);
OT_OSSL_FREE(X509_STORE_CTX, X509_STORE_CTX_free);
OT_OSSL_FREE(X509_VERIFY_PARAM, X509_VERIFY_PARAM_free);
OT_OSSL_FREE(X509_NAME, X509_NAME_free);
OT_OSSL_FREE(X509_ATTRIBUTE, X509_ATTRIBUTE_free);
OT_OSSL_FREE(ASN1_OBJECT, ASN1_OBJECT_free);
// ASN1_INTEGER, ASN1_OCTET_STRING, ASN1_TIME… — один тип asn1_string_st.
OT_OSSL_FREE(ASN1_STRING, ASN1_STRING_free);
OT_OSSL_FREE(ASN1_TYPE, ASN1_TYPE_free);
OT_OSSL_FREE(BIGNUM, BN_free);
OT_OSSL_FREE(CMS_ContentInfo, CMS_ContentInfo_free);
OT_OSSL_FREE(PKCS12, PKCS12_free);
OT_OSSL_FREE(TS_REQ, TS_REQ_free);
OT_OSSL_FREE(TS_RESP, TS_RESP_free);
OT_OSSL_FREE(TS_MSG_IMPRINT, TS_MSG_IMPRINT_free);
OT_OSSL_FREE(TS_TST_INFO, TS_TST_INFO_free);
OT_OSSL_FREE(TS_VERIFY_CTX, TS_VERIFY_CTX_free);
OT_OSSL_FREE(TS_RESP_CTX, TS_RESP_CTX_free);
OT_OSSL_FREE(PKCS7, PKCS7_free);
OT_OSSL_FREE(X509_ALGOR, X509_ALGOR_free);
OT_OSSL_FREE(OCSP_REQUEST, OCSP_REQUEST_free);
OT_OSSL_FREE(OCSP_RESPONSE, OCSP_RESPONSE_free);
OT_OSSL_FREE(OCSP_BASICRESP, OCSP_BASICRESP_free);
OT_OSSL_FREE(OCSP_CERTID, OCSP_CERTID_free);
OT_OSSL_FREE(DIST_POINT, DIST_POINT_free);
OT_OSSL_FREE(AUTHORITY_INFO_ACCESS, AUTHORITY_INFO_ACCESS_free);
OT_OSSL_FREE(GENERAL_NAMES, GENERAL_NAMES_free);
OT_OSSL_FREE(EXTENDED_KEY_USAGE, EXTENDED_KEY_USAGE_free);
#undef OT_OSSL_FREE

// Стопки освобождаются вместе с элементами.
template <>
struct OsslFree<STACK_OF(X509)> {
    void operator()(STACK_OF(X509) * p) const noexcept { sk_X509_pop_free(p, X509_free); }
};
template <>
struct OsslFree<STACK_OF(X509_CRL)> {
    void operator()(STACK_OF(X509_CRL) * p) const noexcept { sk_X509_CRL_pop_free(p, X509_CRL_free); }
};
template <>
struct OsslFree<STACK_OF(DIST_POINT)> {
    void operator()(STACK_OF(DIST_POINT) * p) const noexcept { sk_DIST_POINT_pop_free(p, DIST_POINT_free); }
};

template <typename T>
using Ossl = std::unique_ptr<T, OsslFree<T>>;

// Ошибка OpenSSL: «<что>: <строки очереди ERR через «; »>», code() — первый код очереди (0 — очередь пуста).
// Построение разбирает и очищает очередь потока.
class OsslError : public std::runtime_error {
public:
    OsslError(unsigned long code, const std::string& message) : std::runtime_error(message), code_(code) {}

    // Снять очередь ERR потока в исключение (what — операция: «CMS_sign», «PKCS12_parse»…).
    static OsslError fromQueue(std::string_view what);

    unsigned long code() const noexcept { return code_; }
    // Причина первой ошибки (ERR_GET_REASON) — для разбора неверного пароля и т. п.
    int reason() const noexcept;
    int library() const noexcept;

private:
    unsigned long code_;
};

// Отрезок файла [offset, offset + length) — элемент /ByteRange.
struct ByteRange {
    std::int64_t offset = 0;
    std::int64_t length = 0;
};

namespace ossl {

// Версия библиотеки во время выполнения (OpenSSL_version_num) и строка «OpenSSL 3.5.4 30 Sep 2025».
unsigned long versionNumber() noexcept;
std::string versionText();
// Флаг CMS_NO_SIGNING_TIME доступен (OpenSSL ≥ 3.5.0) — проверяется и при сборке (static_assert).
bool hasSigningTimeFlag() noexcept;

// Код возврата OpenSSL: 1 — успех; иначе OsslError::fromQueue(what).
void check(int rc, std::string_view what);
template <typename T>
T* checkPtr(T* p, std::string_view what) {
    if (p == nullptr) {
        throw OsslError::fromQueue(what);
    }
    return p;
}

// Провайдер legacy (RC2-40, 3DES старых .pfx) — загрузить один раз по требованию, default остаётся.
// false — не загрузился (в статической сборке vcpkg он встроен в libcrypto: no-module).
bool ensureLegacyProvider();

// BIO только для чтения над копией байтов.
Ossl<BIO> memBio(std::span<const std::uint8_t> data);
// Пустой BIO в памяти для записи и его содержимое.
Ossl<BIO> newMemBio();
Bytes bioBytes(BIO* bio);

// BIO только для чтения: части файла по порядку, подряд, без «дыр» между ними. Файл открывается
// _wfopen (путь UTF-16), читается потоком. Отрицательные, пересекающиеся, не по возрастанию или выходящие за
// конец файла отрезки и нет файла — std::runtime_error сразу.
Ossl<BIO> openRangeBio(const std::filesystem::path& file, std::span<const ByteRange> ranges);
// Сумма длин.
std::int64_t rangeLength(std::span<const ByteRange> ranges);

// SHA-256 (и другие EVP_MD) байтов и частей файла.
Bytes sha256(std::span<const std::uint8_t> data);
Bytes sha256(std::string_view data);
Bytes digestRanges(const EVP_MD* md, const std::filesystem::path& file, std::span<const ByteRange> ranges);
Bytes sha256Ranges(const std::filesystem::path& file, std::span<const ByteRange> ranges);

// Байты в верхнем шестнадцатеричном виде («00AB10») — ключи VRI, серийные номера в журнале.
std::string toHex(std::span<const std::uint8_t> data);
// Base64 (RFC 4648, без переносов). decode: пробелы и переводы строк пропускаются; не base64 —
// std::runtime_error.
std::string base64Encode(std::span<const std::uint8_t> data);
Bytes base64Decode(std::string_view text);

// DER объекта (i2d_*) в Bytes; ошибка — OsslError.
template <typename T, typename F>
Bytes toDer(const T* obj, F i2d, std::string_view what) {
    unsigned char* der = nullptr;
    const int len = i2d(const_cast<T*>(obj), &der);
    if (len <= 0 || der == nullptr) {
        throw OsslError::fromQueue(what);
    }
    Bytes out(der, der + len);
    OPENSSL_free(der);
    return out;
}

}  // namespace ossl
}  // namespace ot::sign
