#include "services/dpapi.h"

#include <windows.h>
#include <dpapi.h>

#include <spdlog/spdlog.h>

#pragma comment(lib, "crypt32.lib")

namespace ot {

namespace dpapi {

QByteArray defaultEntropy() { return QByteArrayLiteral("AcroBatya identity secret v1"); }

QByteArray protect(const QByteArray& plain, const QByteArray& entropy) {
    DATA_BLOB in{static_cast<DWORD>(plain.size()),
                 reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()))};
    DATA_BLOB salt{static_cast<DWORD>(entropy.size()),
                   reinterpret_cast<BYTE*>(const_cast<char*>(entropy.constData()))};
    DATA_BLOB out{0, nullptr};
    if (CryptProtectData(&in, L"AcroBatya", entropy.isEmpty() ? nullptr : &salt, nullptr, nullptr,
                         CRYPTPROTECT_UI_FORBIDDEN, &out) == FALSE) {
        spdlog::error("dpapi: CryptProtectData failed ({})", GetLastError());
        return {};
    }
    QByteArray blob(reinterpret_cast<const char*>(out.pbData), static_cast<qsizetype>(out.cbData));
    LocalFree(out.pbData);
    return blob;
}

std::optional<QByteArray> unprotect(const QByteArray& blob, const QByteArray& entropy) {
    DATA_BLOB in{static_cast<DWORD>(blob.size()),
                 reinterpret_cast<BYTE*>(const_cast<char*>(blob.constData()))};
    DATA_BLOB salt{static_cast<DWORD>(entropy.size()),
                   reinterpret_cast<BYTE*>(const_cast<char*>(entropy.constData()))};
    DATA_BLOB out{0, nullptr};
    if (CryptUnprotectData(&in, nullptr, entropy.isEmpty() ? nullptr : &salt, nullptr, nullptr,
                           CRYPTPROTECT_UI_FORBIDDEN, &out) == FALSE) {
        return std::nullopt;
    }
    QByteArray plain(reinterpret_cast<const char*>(out.pbData), static_cast<qsizetype>(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return plain;
}

}  // namespace dpapi

sign::Bytes DpapiProtector::protect(std::span<const std::uint8_t> plain) {
    const QByteArray blob = dpapi::protect(
        QByteArray(reinterpret_cast<const char*>(plain.data()), static_cast<qsizetype>(plain.size())));
    return sign::Bytes(blob.begin(), blob.end());
}

std::optional<sign::Bytes> DpapiProtector::unprotect(std::span<const std::uint8_t> secret) {
    const std::optional<QByteArray> plain = dpapi::unprotect(
        QByteArray(reinterpret_cast<const char*>(secret.data()), static_cast<qsizetype>(secret.size())));
    if (!plain) {
        return std::nullopt;
    }
    return sign::Bytes(plain->begin(), plain->end());
}

}  // namespace ot
