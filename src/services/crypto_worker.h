// Проверка подписей вне окна и вне потока документа (план 4.6, п. 2; эталон 3.8 «Валидатор», 4.1; sign-25 —
// проверка при открытии, sign-26 — панель): файл вкладки открывается своей копией в пуле CryptoWorker (два
// потока) — запросы OCSP/CRL/AIA по сети (до 15 с каждый) не держат поток документа, тайлы и правки идут
// своим чередом; зашифрованный файл (пароль знает только сессия) и документ без файла — задачей сессии.
// Проверяется записанный файл: подписи — свойство байтов на диске, несохранённые правки на них не влияют.
// Параметры — VerifyRequest: якоря (снимок TrustService), сеть (выключатель trust/network; «выключена» —
// только данные файла), политика отзыва, просроченные метки, время проверки (Установки → Подписи → Проверка,
// ключи signatures/…). Поток: вызов — любой; результат — QFuture.
#pragma once

#include <QFuture>
#include <QObject>
#include <QString>
#include <QThreadPool>

#include <memory>

#include "core/sign/trust_store.h"
#include "core/sign/verifier.h"

namespace ot {

class DocumentSession;
class Settings;

struct VerifyRequest {
    std::shared_ptr<const sign::TrustStore> trust;
    bool network = true;  // OCSP, CRL, AIA через httpFetcher(); false — только данные файла
    sign::RevocationPolicy revocation = sign::RevocationPolicy::BestEffort;
    bool acceptExpiredTimestamps = true;

    // Из Установок и хранилища доверия программы.
    static VerifyRequest fromSettings(const Settings& settings);
    sign::VerifyOptions options() const;  // без fetcher (его ставит исполнитель)
};

class CryptoWorker : public QObject {
    Q_OBJECT
public:
    static CryptoWorker& shared();
    ~CryptoWorker() override;

    // Проверить файл документа сессии; ошибка открытия или проверки — исключение в QFuture.
    QFuture<sign::DocumentVerification> verify(DocumentSession& session, VerifyRequest request);
    // То же для файла без сессии (тесты, «Просмотреть подписанную версию»).
    QFuture<sign::DocumentVerification> verifyFile(const QString& path, VerifyRequest request);

    // Ждать окончания всех проверок (выход программы, тесты).
    void waitForDone();

private:
    CryptoWorker();

    QThreadPool pool_;
};

}  // namespace ot
