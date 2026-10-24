// Удостоверение из файла PKCS#12 (.pfx/.p12) — эталон 3.8 «Диалог подписания», sign-06 «Digital ID from a
// file» (план 4.1, п. 3). Файл читает вызывающий (QFile в services/ui — путь не уходит в OpenSSL), сюда —
// байты и пароль UTF-8. AES-256/PBKDF2 (по умолчанию OpenSSL 3 и импортируемый Windows) читается сразу;
// RC2-40/RC4 старых файлов — с провайдером legacy, который грузится при первой нужде.
#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "core/sign/ossl.h"

namespace ot::sign {

enum class Pkcs12Status {
    Ok,
    BadPassword,    // MAC не сошёлся или не расшифровалось с этим паролем
    Damaged,        // не PKCS#12 или испорчен
    NoKey,          // нет закрытого ключа
    NoCertificate,  // нет сертификата к ключу
    Unsupported,    // алгоритм не поддерживается даже с legacy
};

class Pkcs12Error : public std::runtime_error {
public:
    Pkcs12Error(Pkcs12Status status, const std::string& message)
        : std::runtime_error(message), status_(status) {}
    Pkcs12Status status() const noexcept { return status_; }

private:
    Pkcs12Status status_;
};

// Ключ подписи с сертификатом и цепочкой.
struct SigningKey {
    Ossl<EVP_PKEY> key;
    Ossl<X509> cert;
    Ossl<STACK_OF(X509)> chain;  // остальные сертификаты (без подписанта), не null
    std::string friendlyName;    // friendlyName сертификата (UTF-8), может быть пусто
};

// Разобрать PKCS#12. Ошибка — Pkcs12Error со статусом.
SigningKey loadPkcs12(std::span<const std::uint8_t> pfx, std::string_view passwordUtf8);

}  // namespace ot::sign
