#include "services/sign_ops.h"

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

}  // namespace ot::signing
