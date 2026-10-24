// Помощники ui-тестов подписей (план 4.6): фикстуры, сеть подписей в процессе (служба времени тестовой PKI
// отвечает на любой адрес RFC 3161 — в том числе DigiCert из серверов по умолчанию; OCSP, CRL и сертификаты
// ЦС — по адресам http://pki.acrobatya.test/…), доверие к корню PKI в хранилище программы и проверка
// записанного файла ядром. Без Python и без сети (эталон 8.3).
#pragma once

#include <QString>

#include <atomic>
#include <memory>

#include "core/sign/trust_store.h"
#include "core/sign/verifier.h"
#include "core/util/http_fetcher.h"
#include "services/http_fetcher.h"

namespace ot::test {

// <исходники>/tests/fixtures/<rel>
QString fixturePath(const QString& rel);
// Копия фикстуры в каталог dir под именем name.
QString copyFixture(const QString& rel, const QString& dir, const QString& name);

// Удостоверение тестовой PKI и его пароль.
QString pkiPfx(const char* name = "signer-rsa.pfx");
inline QString pkiPassword() { return QStringLiteral("acrobatya"); }

// Сеть подписей процесса на время жизни объекта (ScopedHttpFetcher).
class PkiNetwork {
public:
    PkiNetwork();
    ~PkiNetwork();
    PkiNetwork(const PkiNetwork&) = delete;
    PkiNetwork& operator=(const PkiNetwork&) = delete;

    // Служба времени «недоступна» (сетевая ошибка) — для вопроса «подписать без метки?».
    void setTsaDown(bool down) { tsaDown_ = down; }
    int tsaRequests() const { return tsaRequests_; }
    ot::net::FakeHttpFetcher& fetcher() { return fake_; }

private:
    ot::net::FakeHttpFetcher fake_;
    std::unique_ptr<ot::ScopedHttpFetcher> scope_;
    std::atomic<bool> tsaDown_{false};
    std::atomic<int> tsaRequests_{0};
};

// Хранилище с корнем PKI (подписи и сертифицированные документы).
ot::sign::TrustStore pkiTrust();
// Корень PKI — якорь в хранилище программы (TrustService); false — уже был.
bool trustPkiRoot();
// Проверить файл ядром (якоря — trust; nullptr — без доверия, отзыв не проверяется).
ot::sign::DocumentVerification verifyFile(const QString& path, const ot::sign::TrustStore* trust);

}  // namespace ot::test
