// Тестовая PKI (план 4.1; tools/pki/make_test_pki.ps1 → tests/fixtures/pki, состав — pki.json) и внешний
// openssl.exe — независимая проверка подписей (`openssl cms -verify -cades`) и TSA-ответов в тестах.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/sign/ossl.h"

namespace ot::test {

// tests/fixtures/pki
std::filesystem::path pkiDir();
// Байты файла PKI (нет файла — std::runtime_error).
std::string readPki(const char* name);

// Сертификат и ключ PKI (PEM) — объекты OpenSSL.
ot::sign::Ossl<X509> pkiCert(const char* name);
ot::sign::Ossl<EVP_PKEY> pkiKey(const char* name);
// Хранилище с корнем PKI (root.pem) — якорь цепочек подписантов, TSA и OCSP.
ot::sign::Ossl<X509_STORE> pkiRootStore();

// Ответ службы времени PKI (tsa.pem/tsa.key, политика 1.2.3.4.1, точность 1 с, ESS v2) на запрос query —
// в процессе (TS_RESP_CTX), без Python: обработчик FakeHttpFetcher.
std::vector<std::uint8_t> localTsaReply(std::span<const std::uint8_t> query);

// openssl.exe для внешней проверки: OTTISK_OPENSSL, Git for Windows (mingw64, затем usr); нет — nullopt (тест
// — SKIP).
std::optional<std::filesystem::path> opensslExe();

struct ProcessResult {
    int exitCode = -1;   // -1 — не запустился или не уложился в таймаут
    std::string output;  // stdout + stderr
};
// Запустить программу и дождаться (без окна, без stdin).
ProcessResult runProcess(const std::filesystem::path& exe,
                         const std::vector<std::filesystem::path::string_type>& args, int timeoutMs = 60000);

}  // namespace ot::test
