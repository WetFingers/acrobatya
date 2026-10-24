#include "core/sign/doc_timestamp.h"

#include <spdlog/spdlog.h>

#include <ctime>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/sign/sign_field.h"
#include "core/util/paths.h"

namespace ot::sign {

TimestampSigner::TimestampSigner(std::shared_ptr<TsaClient> tsa)
    : PdfSigner(tsa ? tsa->config().digest : DigestAlgorithm::Sha256), tsa_(std::move(tsa)) {}

Bytes TimestampSigner::contents(std::span<const std::uint8_t> digest) {
    try {
        Bytes token = tsa_->timestamp(digest);
        info_ = tsa_->lastInfo();
        return token;
    } catch (const TsaError& e) {
        throw SignError(SignStatus::TimestampFailed, e.what(), SaveError::None, e.status());
    }
}

DocTimestampResult addDocumentTimestamp(Document& doc, std::shared_ptr<TsaClient> tsa,
                                        const std::filesystem::path& target) {
    if (doc.pdf() == nullptr) {
        throw SignError(SignStatus::NotPdf, "only PDF documents are timestamped");
    }
    if (!tsa) {
        throw SignError(SignStatus::TimestampFailed, "no timestamp service", SaveError::None,
                        TsaStatus::NetworkError);
    }
    if (!doc.allowed(Perm::FillForms)) {
        throw SignError(SignStatus::NotAllowed, "document permissions do not allow signing");
    }
    auto signer = std::make_shared<TimestampSigner>(std::move(tsa));

    detail::FieldJob job;
    job.operation = "Timestamp document";
    job.placement.field = detail::nextFieldName(doc, "Timestamp");
    job.placement.page = 0;  // невидимое поле на первой странице
    job.time = static_cast<std::int64_t>(std::time(nullptr));
    job.before = [](fz_context* ctx, pdf_document* pdf) { detail::ensureEsic(ctx, pdf, 1); };
    job.patch = [](fz_context* ctx, pdf_document*, pdf_obj* v, bool) {
        mu::call(ctx, [&] {
            pdf_dict_put_drop(ctx, v, PDF_NAME(Type), pdf_new_name(ctx, "DocTimeStamp"));
            pdf_dict_put_drop(ctx, v, PDF_NAME(SubFilter), pdf_new_name(ctx, "ETSI.RFC3161"));
            pdf_dict_del(ctx, v, PDF_NAME(M));
            pdf_dict_del(ctx, v, PDF_NAME(Reference));
        });
    };

    const detail::FieldOutcome outcome = detail::signField(doc, job, *signer, target);
    DocTimestampResult result;
    result.field = outcome.field;
    result.contentsSize = signer->contentsSize();
    result.timestamp = signer->info();
    result.save = outcome.save;
    spdlog::info("sign: {} document timestamp \"{}\" ({} bytes, {})", paths::toUtf8(doc.path()), result.field,
                 result.contentsSize, result.timestamp.genTime);
    return result;
}

}  // namespace ot::sign
