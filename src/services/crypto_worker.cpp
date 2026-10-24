#include "services/crypto_worker.h"

#include <QCoreApplication>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include <spdlog/spdlog.h>

#include <stdexcept>

#include "core/doc/document.h"
#include "services/document_session.h"
#include "services/http_fetcher.h"
#include "services/settings.h"
#include "services/task_context.h"
#include "services/trust_service.h"

namespace ot {

namespace crypto_worker {

sign::RevocationPolicy revocationPolicy(const QString& name) {
    if (name == QLatin1String("required")) {
        return sign::RevocationPolicy::Required;
    }
    if (name == QLatin1String("never")) {
        return sign::RevocationPolicy::Never;
    }
    return sign::RevocationPolicy::BestEffort;
}

sign::DocumentVerification run(Document& doc, const VerifyRequest& request) {
    sign::VerifyOptions o = request.options();
    if (request.network) {
        o.fetcher = &httpFetcher();
    }
    return sign::verifyDocument(doc, o);
}

}  // namespace crypto_worker

VerifyRequest VerifyRequest::fromSettings(const Settings& settings) {
    VerifyRequest r;
    r.trust = TrustService::shared().snapshot();
    r.network = networkAccess(settings) != NetworkAccess::Off;
    r.revocation = crypto_worker::revocationPolicy(settings.get(settings::kVerifyRevocation));
    r.acceptExpiredTimestamps = settings.get(settings::kVerifyExpiredTimestamps);
    const QString time = settings.get(settings::kVerifyTime);
    r.time = time == QLatin1String("current")  ? sign::VerificationTime::Current
             : time == QLatin1String("secure") ? sign::VerificationTime::SecureTime
                                               : sign::VerificationTime::SigningTime;
    return r;
}

sign::VerifyOptions VerifyRequest::options() const {
    sign::VerifyOptions o;
    o.trust = trust.get();
    o.revocation = revocation;
    o.acceptExpiredTimestamps = acceptExpiredTimestamps;
    o.time = time;
    return o;
}

CryptoWorker& CryptoWorker::shared() {
    static CryptoWorker worker;
    // Проверка, идущая при выходе, дожидается из деструктора QCoreApplication, пока Qt и MuPDF живы: иначе
    // её дожидается деструктор статика уже после main — зависание или падение на выходе (рубеж Р2).
    static const bool drained = [] {
        qAddPostRoutine([] {
            CryptoWorker& w = shared();
            w.pool_.clear();
            w.pool_.waitForDone();
        });
        return true;
    }();
    (void)drained;
    return worker;
}

CryptoWorker::CryptoWorker() {
    pool_.setMaxThreadCount(2);
    pool_.setObjectName(QStringLiteral("CryptoWorker"));
}

CryptoWorker::~CryptoWorker() { pool_.waitForDone(); }

void CryptoWorker::waitForDone() { pool_.waitForDone(); }

QFuture<sign::DocumentVerification> CryptoWorker::verifyFile(const QString& path, VerifyRequest request) {
    return QtConcurrent::run(&pool_, [path, request = std::move(request)] {
        auto doc = Document::openOrThrow(std::filesystem::path(path.toStdWString()));
        return crypto_worker::run(*doc, request);
    });
}

QFuture<sign::DocumentVerification> CryptoWorker::verify(DocumentSession& session, VerifyRequest request) {
    QPointer<DocumentSession> guard(&session);
    const QString path = session.path();
    return QtConcurrent::run(
        &pool_, [guard, path, request = std::move(request)]() -> sign::DocumentVerification {
            if (!path.isEmpty()) {
                OpenResult opened = Document::open(std::filesystem::path(path.toStdWString()));
                if (opened.ok && opened.document) {
                    return crypto_worker::run(*opened.document, request);
                }
                if (!opened.needsPassword) {
                    throw std::runtime_error("cannot open " + path.toStdString() + " for verification");
                }
            }
            // Зашифрованный файл или документ без файла — задачей сессии (пароль и документ — у неё).
            if (guard == nullptr) {
                throw std::runtime_error("document closed before verification");
            }
            spdlog::debug("verify: {} in the document thread", path.toStdString());
            return guard
                ->post([request](TaskContext& tc) { return crypto_worker::run(tc.document(), request); },
                       TaskPriority::Low, QStringLiteral("Verify signatures"))
                .result();
        });
}

}  // namespace ot
