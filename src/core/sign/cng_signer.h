// Подпись ключом из хранилища Windows — CNG (эталон 3.8 «CNG-путь», 11 риск 2; план 4.3, п. 2; sign-08 —
// подпись сертификатом хранилища, sign-06 — удостоверение на устройстве; смарт-карты и токены с
// мини-драйвером — через их KSP, вместо PKCS#11 sign-07). Ключ не экспортируется:
// CryptAcquireCertificatePrivateKey(PREFER_NCRYPT, CACHE, COMPARE_KEY, WINDOW_HANDLE с hwnd) →
// NCRYPT_KEY_HANDLE по CERT_NCRYPT_KEY_SPEC (ключ старого CSP — NCryptTranslateHandle, не переводится — RSA
// через CryptoAPI CryptSignHash, только PKCS#1); подпись — NCryptSignHash: RSA PKCS#1 v1.5
// (BCRYPT_PKCS1_PADDING_INFO) или PSS (BCRYPT_PSS_PADDING_INFO, соль = длина хэша, NCRYPT_PAD_PSS_FLAG —
// только «сырой» хэш: в CMS RSA-PSS не пишется, эталон 3.8), ECDSA — IEEE P1363 r‖s → DER ECDSA-Sig-Value
// (ECDSA_SIG_set0 + i2d_ECDSA_SIG). PIN спрашивает Windows (KSP карты) с окном-владельцем; silent — без окон
// (нужно окно — CngError::UiRequired). CngIdentity — сертификат хранилища с цепочкой (CertGetCertificateChain
// только из кэша, без сети) и ключом; cngCmsFromDigest — CAdES-B через ExternalCmsBuilder. Заголовок без
// <windows.h>: дескрипторы — std::uintptr_t.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "core/sign/cms.h"
#include "core/sign/cng_store.h"
#include "core/sign/ossl.h"

namespace ot::sign {

class CngError : public std::runtime_error {
public:
    enum Status {
        NotFound,        // сертификата с таким отпечатком нет в хранилище
        NoPrivateKey,    // у сертификата нет закрытого ключа (или контейнер ключа пропал)
        LegacyProvider,  // ключ старого CSP не переводится в CNG
        Cancelled,       // человек отменил ввод PIN или выбор
        UiRequired,      // silent, а KSP нужно окно (PIN)
        UnsupportedKey,  // не RSA и не ECDSA
        SignFailed,      // NCryptSignHash и прочие отказы CNG
    };
    CngError(Status status, const std::string& message, long code = 0)
        : std::runtime_error(message), status_(status), code_(code) {}
    Status status() const noexcept { return status_; }
    long code() const noexcept { return code_; }  // SECURITY_STATUS / HRESULT / GetLastError

private:
    Status status_;
    long code_;
};

enum class CngPadding { Pkcs1, Pss };
enum class CngKeyKind { Rsa, Ec };

struct CngOptions {
    std::uintptr_t window = 0;  // HWND владельца окна PIN (0 — без владельца)
    bool silent = false;        // без окон (тесты, проверка доступности ключа)
};

// Закрытый ключ сертификата хранилища (NCRYPT_KEY_HANDLE); только перемещение.
class CngKey {
public:
    // Ошибки — CngError (NoPrivateKey, LegacyProvider, UnsupportedKey, Cancelled, UiRequired).
    static CngKey acquire(const cng::CertContext& cert, const CngOptions& options = {});

    CngKey(CngKey&& other) noexcept;
    CngKey& operator=(CngKey&& other) noexcept;
    CngKey(const CngKey&) = delete;
    CngKey& operator=(const CngKey&) = delete;
    ~CngKey();

    CngKeyKind kind() const noexcept { return kind_; }
    // Подпись хэша (длина — как у digest, иначе std::invalid_argument): RSA — PKCS#1 v1.5 или PSS, ECDSA —
    // DER. Ошибка — CngError.
    Bytes signHash(std::span<const std::uint8_t> hash, DigestAlgorithm digest,
                   CngPadding padding = CngPadding::Pkcs1) const;

private:
    CngKey() = default;
    void reset() noexcept;
    Bytes capiSignHash(std::span<const std::uint8_t> hash, DigestAlgorithm digest, CngPadding padding) const;
    std::uintptr_t reopenWithAes() const;  // HCRYPTPROV того же контейнера в PROV_RSA_AES (SHA-2); 0 — нет

    std::uintptr_t key_ = 0;       // NCRYPT_KEY_HANDLE
    std::uintptr_t provider_ = 0;  // NCRYPT_PROV_HANDLE после NCryptTranslateHandle
    std::uintptr_t capi_ = 0;      // HCRYPTPROV ключа старого CSP, который не переводится в CNG (только RSA)
    unsigned long capiSpec_ = 0;   // AT_SIGNATURE / AT_KEYEXCHANGE
    bool owned_ = false;           // освобождать key_/capi_ (pfCallerFreeProvOrNCryptKey)
    CngKeyKind kind_ = CngKeyKind::Rsa;
    bool silent_ = false;
    cng::CertContext cert_;  // кэшированный дескриптор ключа живёт в контексте сертификата
};

// IEEE P1363 r‖s (CNG) → DER ECDSA-Sig-Value; нечётная длина или пусто — std::invalid_argument.
Bytes ecdsaP1363ToDer(std::span<const std::uint8_t> rs);

// Удостоверение хранилища Windows: сертификат, цепочка (без подписанта) и ключ.
struct CngIdentity {
    cng::CertContext context;
    Ossl<X509> cert;
    Ossl<STACK_OF(X509)> chain;
    std::shared_ptr<CngKey> key;

    // Сертификат по отпечатку в store, цепочка, ключ. Нет сертификата — CngError::NotFound, нет ключа —
    // NoPrivateKey.
    static CngIdentity open(const cng::CertStore& store, std::string_view thumbprint,
                            const CngOptions& options = {});
};

// Цепочка сертификата без подписанта до корня (CertGetCertificateChain из хранилищ Windows и хранилища
// сертификата, без загрузки по сети); не строится — пусто.
Ossl<STACK_OF(X509)> certificateChain(const cng::CertContext& cert);

// CAdES-B detached по хэшу данных ключом CNG (RSA — PKCS#1 v1.5, ECDSA — DER) — ExternalCmsBuilder.
Bytes cngCmsFromDigest(const CngIdentity& identity, std::span<const std::uint8_t> digest,
                       DigestAlgorithm algorithm = DigestAlgorithm::Sha256);

}  // namespace ot::sign
