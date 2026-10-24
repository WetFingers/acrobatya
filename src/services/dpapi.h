// DPAPI текущего пользователя (план 4.2, п. 4; вопрос 12.5 — пароли .pfx и политик «через DPAPI по флажку»;
// эталон 3.8): CryptProtectData/CryptUnprotectData без окон (CRYPTPROTECT_UI_FORBIDDEN), с энтропией
// программы — блок другой программы того же пользователя не подходит. Блоки хранятся в state.sqlite
// (удостоверения) и политиках (7.6); открытый текст — только в памяти. DpapiProtector — защитник
// sign::IdentityStore.
#pragma once

#include <QByteArray>

#include <optional>

#include "core/sign/identity.h"

namespace ot {

namespace dpapi {

// Энтропия программы по умолчанию.
QByteArray defaultEntropy();
// Блок DPAPI; ошибка — пустой.
QByteArray protect(const QByteArray& plain, const QByteArray& entropy = defaultEntropy());
// Открытый текст; испорченный, чужой (другая энтропия или пользователь) — nullopt.
std::optional<QByteArray> unprotect(const QByteArray& blob, const QByteArray& entropy = defaultEntropy());

}  // namespace dpapi

class DpapiProtector : public sign::SecretProtector {
public:
    sign::Bytes protect(std::span<const std::uint8_t> plain) override;
    std::optional<sign::Bytes> unprotect(std::span<const std::uint8_t> secret) override;
};

}  // namespace ot
