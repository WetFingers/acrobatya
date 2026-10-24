#include "services/sign_ops.h"

#include "core/doc/document.h"
#include "core/doc/revisions.h"
#include "services/http_fetcher.h"
#include "services/task_context.h"

namespace ot::signing {

QFuture<std::vector<sign::SignatureField>> fields(DocumentSession& session, TaskPriority priority) {
    return session.post([](TaskContext& tc) { return sign::signatureFields(tc.document()); }, priority,
                        QStringLiteral("Signature fields"));
}

QFuture<sign::WarningScan> warnings(DocumentSession& session) {
    return session.post([](TaskContext& tc) { return sign::scanWarnings(tc.document()); }, TaskPriority::High,
                        QStringLiteral("Document warnings"));
}

QFuture<sign::CertifyBlock> certifyBlock(DocumentSession& session) {
    return session.post([](TaskContext& tc) { return sign::certifyBlock(tc.document()); }, TaskPriority::High,
                        QStringLiteral("Certify check"));
}

QFuture<sign::DocMdp> docMdp(DocumentSession& session) {
    return session.post([](TaskContext& tc) { return sign::readDocMdp(tc.document()); }, TaskPriority::High,
                        QStringLiteral("DocMDP"));
}

QFuture<sign::ClearBlock> clearBlock(DocumentSession& session, QString field) {
    return session.post(
        [field = field.toStdString()](TaskContext& tc) { return sign::clearBlock(tc.document(), field); },
        TaskPriority::High, QStringLiteral("Clear check"));
}

QFuture<void> clearSignature(DocumentSession& session, QString field) {
    return session.edit(QStringLiteral("Clear signature"), [field = field.toStdString()](TaskContext& tc) {
        int page = -1;
        for (const sign::SignatureField& f : sign::signatureFields(tc.document())) {
            if (f.name == field) {
                page = f.page;
            }
        }
        sign::clearSignature(tc.document(), field);
        if (page >= 0) {
            tc.touchPage(page);
        }
    });
}

QFuture<QString> signedVersion(DocumentSession& session, QString field) {
    return session.post(
        [field = field.toStdString()](TaskContext& tc) {
            for (const revisions::SignatureInfo& s : revisions::signatures(tc.document())) {
                if (s.field == field) {
                    const std::filesystem::path path = revisions::signedRevisionBytes(tc.document(), s.index);
                    return QString::fromStdWString(path.wstring());
                }
            }
            return QString();
        },
        TaskPriority::High, QStringLiteral("Signed version"));
}

QFuture<sign::LtvDecision> ltvDecision(DocumentSession& session,
                                       std::shared_ptr<const sign::TrustStore> trust, sign::LtvAutoAdd mode) {
    return session.post(
        [trust = std::move(trust), mode](TaskContext& tc) {
            if (mode == sign::LtvAutoAdd::Never) {
                return sign::LtvDecision::Skip;
            }
            sign::LtvOptions options;
            options.trust = trust.get();
            options.fetcher = &httpFetcher();
            const sign::LtvCollection collected = sign::collectLtv(tc.document(), options);
            std::error_code ec;
            const std::uintmax_t size = std::filesystem::file_size(tc.document().filePath(), ec);
            return sign::decideAutoLtv(mode, ec ? 0 : size, collected.added().bytes());
        },
        TaskPriority::Low, QStringLiteral("LTV decision"));
}

}  // namespace ot::signing
