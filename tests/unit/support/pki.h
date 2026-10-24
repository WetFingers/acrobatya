// Тестовая PKI (план 4.1; tools/pki/make_test_pki.ps1 → tests/fixtures/pki, состав — pki.json) и внешний
// openssl.exe — независимая проверка подписей (`openssl cms -verify -cades`) и TSA-ответов в тестах.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ot::test {

// tests/fixtures/pki
std::filesystem::path pkiDir();
// Байты файла PKI (нет файла — std::runtime_error).
std::string readPki(const char* name);

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
