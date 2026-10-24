// Помощники ui-тестов подписей (план 4.6): фикстуры, сеть подписей в процессе (служба времени тестовой PKI
// отвечает на любой адрес RFC 3161 — в том числе DigiCert из серверов по умолчанию; OCSP, CRL и сертификаты
// ЦС — по адресам http://pki.acrobatya.test/…), доверие к корню PKI в хранилище программы и проверка
// записанного файла ядром. Без Python и без сети (эталон 8.3).
#pragma once

#include <QString>

#include <atomic>
#include <memory>
#include <optional>
#include <string>

#include "core/sign/docmdp.h"
#include "core/sign/signer.h"
#include "core/sign/trust_store.h"
#include "core/sign/verifier.h"
#include "core/util/geometry.h"
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

// Подписать файл ядром удостоверением signer-rsa.pfx (как будто подписал кто-то другой): уровень, служба
// времени и сеть — network (служба PKI в процессе), сертификация, рамка (пусто — невидимая), блокировка.
struct SignFileOptions {
    ot::sign::SignLevel level = ot::sign::SignLevel::B;
    std::optional<ot::sign::MdpPermission> certify;
    ot::RectF rect{72, 600, 272, 660};
    bool lock = false;
    PkiNetwork* network = nullptr;  // B-T/LT/LTA: служба времени и OCSP
    std::string reason;
};
void signFile(const QString& path, const SignFileOptions& options = {});
// Правка после подписи мимо программы (каталог /PageMode) инкрементом — изменение, которое подпись не
// разрешает.
void tamperFile(const QString& path);

// Хранилище с корнем PKI (подписи и сертифицированные документы).
ot::sign::TrustStore pkiTrust();
// Корень PKI — якорь в хранилище программы (TrustService); false — уже был.
bool trustPkiRoot();
// Убрать корень PKI из хранилища программы; false — его там не было.
bool untrustPkiRoot();
// Проверить файл ядром (якоря — trust; nullptr — без доверия, отзыв не проверяется).
ot::sign::DocumentVerification verifyFile(const QString& path, const ot::sign::TrustStore* trust);

}  // namespace ot::test
