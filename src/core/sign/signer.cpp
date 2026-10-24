#include "core/sign/signer.h"

#include <openssl/evp.h>
#include <openssl/x509.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cstring>
#include <ctime>
#include <vector>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/sign/cng_signer.h"
#include "core/sign/cng_store.h"
#include "core/sign/doc_timestamp.h"
#include "core/sign/ltv.h"
#include "core/sign/sign_field.h"
#include "core/sign/x509_info.h"
#include "core/util/paths.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace ot::sign {

namespace fs = std::filesystem;

// Объект MuPDF: pdf_pkcs7_signer со счётчиком ссылок; держит PdfSigner, пока MuPDF держит подпись.
struct MuSigner : pdf_pkcs7_signer {
    std::atomic<int> refs{1};
    std::shared_ptr<PdfSigner> owner;
};

// Колбэки MuPDF: ни одно исключение C++ не выходит в C (create_digest — 0, имя — nullptr).
struct MuSignerAccess {
    static pdf_pkcs7_signer* keep(fz_context*, pdf_pkcs7_signer* s) {
        static_cast<MuSigner*>(s)->refs.fetch_add(1);
        return s;
    }

    static void drop(fz_context*, pdf_pkcs7_signer* s) {
        auto* m = static_cast<MuSigner*>(s);
        if (m->refs.fetch_sub(1) == 1) {
            delete m;
        }
    }

    static size_t maxDigestSize(fz_context*, pdf_pkcs7_signer*) { return PdfSigner::kMaxContentsSize; }

    static pdf_pkcs7_distinguished_name* signingName(fz_context* ctx, pdf_pkcs7_signer* s) {
        std::string cn;
        try {
            cn = static_cast<MuSigner*>(s)->owner->signingName();
        } catch (const std::exception&) {
            return nullptr;
        }
        const char* text = cn.c_str();
        pdf_pkcs7_distinguished_name* dn = nullptr;
        try {
            mu::call(ctx, [&] {
                dn = static_cast<pdf_pkcs7_distinguished_name*>(fz_calloc(ctx, 1, sizeof(*dn)));
                dn->cn = fz_strdup(ctx, text);
            });
        } catch (const MuError&) {
            if (dn != nullptr) {
                pdf_signature_drop_distinguished_name(ctx, dn);
            }
            return nullptr;
        }
        return dn;
    }

    static int createDigest(fz_context* ctx, pdf_pkcs7_signer* s, fz_stream* in, unsigned char* out,
                            size_t len) {
        PdfSigner& owner = *static_cast<MuSigner*>(s)->owner;
        owner.error_ = nullptr;
        try {
            Ossl<EVP_MD_CTX> md(ossl::checkPtr(EVP_MD_CTX_new(), "EVP_MD_CTX_new"));
            ossl::check(EVP_DigestInit_ex(md.get(), evpDigest(owner.digest_), nullptr), "EVP_DigestInit_ex");
            std::vector<unsigned char> buffer(64 * 1024);
            unsigned char* data = buffer.data();
            const size_t capacity = buffer.size();
            for (;;) {
                size_t n = 0;
                mu::call(ctx, [&] { n = fz_read(ctx, in, data, capacity); });
                if (n == 0) {
                    break;
                }
                ossl::check(EVP_DigestUpdate(md.get(), data, n), "EVP_DigestUpdate");
            }
            Bytes digest(static_cast<std::size_t>(EVP_MAX_MD_SIZE));
            unsigned int digestLen = 0;
            ossl::check(EVP_DigestFinal_ex(md.get(), digest.data(), &digestLen), "EVP_DigestFinal_ex");
            digest.resize(digestLen);
            const Bytes der = owner.sign(digest);
            if (der.size() > len) {
                throw SignError(SignStatus::TooLarge, "signature contents do not fit the /Contents reserve");
            }
            std::memcpy(out, der.data(), der.size());
            return static_cast<int>(der.size());
        } catch (...) {
            owner.error_ = std::current_exception();
            return 0;
        }
    }
};

// ---------------------------------------------------------------- PdfSigner

PdfSigner::PdfSigner(DigestAlgorithm digest) : digest_(digest) {}

PdfSigner::~PdfSigner() = default;

pdf_pkcs7_signer* PdfSigner::newMuSigner() {
    auto* m = new MuSigner();
    m->keep = &MuSignerAccess::keep;
    m->drop = &MuSignerAccess::drop;
    m->get_signing_name = &MuSignerAccess::signingName;
    m->max_digest_size = &MuSignerAccess::maxDigestSize;
    m->create_digest = &MuSignerAccess::createDigest;
    m->owner = shared_from_this();
    return m;
}

Bytes PdfSigner::sign(std::span<const std::uint8_t> digest) {
    Bytes der = contents(digest);
    if (der.empty()) {
        throw SignError(SignStatus::CmsFailed, "empty signature contents");
    }
    if (der.size() > kMaxContentsSize) {
        throw SignError(SignStatus::TooLarge, "signature contents (" + std::to_string(der.size()) +
                                                  " bytes) exceed the /Contents reserve");
    }
    contentsSize_ = der.size();
    return der;
}

// ---------------------------------------------------------------- AcroBatyaSigner

namespace {

// Отказ CNG → SignError: интерфейс различает «нет сертификата», «нет ключа», «отменён PIN» и прочее.
SignError fromCng(const CngError& e) {
    switch (e.status()) {
        case CngError::NotFound:
            return SignError(SignStatus::NoIdentity, e.what());
        case CngError::NoPrivateKey:
            return SignError(SignStatus::NoPrivateKey, e.what());
        case CngError::Cancelled:
            return SignError(SignStatus::Cancelled, e.what());
        default:
            return SignError(SignStatus::KeyFailed, e.what());
    }
}

}  // namespace

AcroBatyaSigner::AcroBatyaSigner(std::shared_ptr<const SigningKey> identity, DigestAlgorithm digest,
                                 std::shared_ptr<TsaClient> tsa,
                                 std::function<bool(const TsaError&)> withoutTimestamp)
    : PdfSigner(digest),
      identity_(std::move(identity)),
      tsa_(std::move(tsa)),
      withoutTimestamp_(std::move(withoutTimestamp)) {}

AcroBatyaSigner::AcroBatyaSigner(std::shared_ptr<const CngIdentity> identity, DigestAlgorithm digest,
                                 std::shared_ptr<TsaClient> tsa,
                                 std::function<bool(const TsaError&)> withoutTimestamp)
    : PdfSigner(digest),
      windows_(std::move(identity)),
      tsa_(std::move(tsa)),
      withoutTimestamp_(std::move(withoutTimestamp)) {}

AcroBatyaSigner::~AcroBatyaSigner() = default;

Bytes AcroBatyaSigner::cms(std::span<const std::uint8_t> digest) {
    if (windows_) {
        // Хранилище Windows: PKCS7-путь с подписью NCryptSignHash (PIN — окно Windows).
        return cngCmsFromDigest(*windows_, digest, this->digest());
    }
    if (!identity_) {
        throw SignError(SignStatus::NoIdentity, "no signing identity");
    }
    CmsBuilder builder(*identity_, this->digest());
    return builder.buildFromDigest(digest);
}

Bytes AcroBatyaSigner::contents(std::span<const std::uint8_t> digest) {
    timestamped_ = false;
    timestampError_.clear();
    try {
        Bytes der = cms(digest);
        if (!tsa_) {
            return der;
        }
        // B-T: imprint — хэш значения подписи (RFC 3161 прил. A) алгоритмом службы.
        const Bytes value = cmsSignatureValue(der);
        const EVP_MD* md = evpDigest(tsa_->config().digest);
        Bytes imprint(static_cast<std::size_t>(EVP_MAX_MD_SIZE));
        unsigned int imprintLen = 0;
        ossl::check(EVP_Digest(value.data(), value.size(), imprint.data(), &imprintLen, md, nullptr),
                    "EVP_Digest");
        imprint.resize(imprintLen);
        Bytes token;
        try {
            token = tsa_->timestamp(imprint);
        } catch (const TsaError& e) {
            if (withoutTimestamp_ && withoutTimestamp_(e)) {
                spdlog::warn("sign: no timestamp ({}), signing without it", e.what());
                timestampError_ = e.what();
                return der;
            }
            throw SignError(SignStatus::TimestampFailed, e.what(), SaveError::None, e.status());
        }
        der = cmsAddUnsignedTimestamp(der, token);
        timestamped_ = true;
        timestampInfo_ = tsa_->lastInfo();
        return der;
    } catch (const OsslError& e) {
        throw SignError(SignStatus::CmsFailed, e.what());
    } catch (const CngError& e) {
        throw fromCng(e);
    }
}

std::string AcroBatyaSigner::signingName() const {
    X509* cert = windows_ ? windows_->cert.get() : (identity_ ? identity_->cert.get() : nullptr);
    return cert != nullptr ? commonName(X509_get_subject_name(cert)) : std::string();
}

std::shared_ptr<const CngIdentity> openWindowsIdentity(const WindowsCert& cert) {
    if (cng::normalizeThumbprint(cert.thumbprint).empty()) {
        throw SignError(SignStatus::NoIdentity, "not a certificate thumbprint: " + cert.thumbprint);
    }
    CngOptions options;
    options.window = cert.window;
    options.silent = cert.silent;
    try {
        if (cert.store) {
            return std::make_shared<CngIdentity>(CngIdentity::open(*cert.store, cert.thumbprint, options));
        }
        const cng::CertStore personal = cng::CertStore::openPersonal();
        return std::make_shared<CngIdentity>(CngIdentity::open(personal, cert.thumbprint, options));
    } catch (const CngError& e) {
        throw fromCng(e);
    } catch (const cng::StoreError& e) {
        throw SignError(SignStatus::NoIdentity, e.what());
    } catch (const OsslError& e) {
        throw SignError(SignStatus::NoIdentity, e.what());
    }
}

// ---------------------------------------------------------------- signDocument

const char* subFilterName(SubFilter subFilter) noexcept {
    return subFilter == SubFilter::Pkcs7Detached ? "adbe.pkcs7.detached" : "ETSI.CAdES.detached";
}

namespace {

// Картинка и логотип внешнего вида читаются — до записи и правки документа.
void checkAppearanceFiles(const SignRequest& request) {
    const SignatureAppearance& look = request.appearance;
    if (request.placement.rect.empty() && request.placement.field.empty()) {
        return;  // невидимая подпись — вида нет
    }
    std::error_code ec;
    if ((look.imageOnly || look.graphic == AppearanceGraphic::Image) &&
        !fs::is_regular_file(look.image, ec)) {
        throw SignError(SignStatus::BadAppearance, "signature image not found: " + paths::toUtf8(look.image));
    }
    if (!look.logo.empty() && !fs::is_regular_file(look.logo, ec)) {
        throw SignError(SignStatus::BadAppearance, "signature logo not found: " + paths::toUtf8(look.logo));
    }
}

}  // namespace

std::string nextSignatureFieldName(Document& doc) { return detail::nextFieldName(doc, "Signature"); }

namespace {

// LT: /DSS следующей секцией; LTA: ещё метка времени документа. Подпись уже записана — сбой шага не отменяет
// её.
void extendLevel(Document& doc, const SignRequest& request, SignResult& result) {
    LtvOptions options;
    options.trust = request.trust;
    options.fetcher = request.revocationFetcher;
    options.vri = request.vri;
    try {
        const LtvResult l = addValidationInfo(doc, options);
        result.dssWritten = l.dss.written;
        result.dssCertificates = l.dss.certs;
        result.dssOcsps = l.dss.ocsps;
        result.dssCrls = l.dss.crls;
        for (const LtvSignature& s : l.collected.signatures) {
            if (s.field == result.field) {
                result.ltvComplete = s.complete();
                result.ltvMissing = s.missing;
            }
        }
        if (!l.save.ok()) {
            result.ltvError = l.save.message;
        } else if (result.timestamped) {
            result.level = SignLevel::LT;
        }
    } catch (const std::exception& e) {
        result.ltvError = e.what();
    }
    if (!result.ltvError.empty()) {
        spdlog::error("sign: validation info after signing failed: {}", result.ltvError);
        return;
    }
    if (request.level != SignLevel::LTA) {
        return;
    }
    try {
        const DocTimestampResult d = addDocumentTimestamp(doc, request.timestamp);
        result.docTimestamped = true;
        result.docTimestampField = d.field;
        result.docTimestamp = d.timestamp;
        if (result.level == SignLevel::LT) {
            result.level = SignLevel::LTA;
        }
    } catch (const std::exception& e) {
        result.archiveError = e.what();
        spdlog::error("sign: document time-stamp after signing failed: {}", result.archiveError);
    }
}

}  // namespace

const char* signLevelName(SignLevel level) {
    switch (level) {
        case SignLevel::B:
            return "B";
        case SignLevel::T:
            return "T";
        case SignLevel::LT:
            return "LT";
        case SignLevel::LTA:
            return "LTA";
    }
    return "?";
}

SignResult signDocument(Document& doc, const SignRequest& request, const fs::path& target) {
    if (doc.pdf() == nullptr) {
        throw SignError(SignStatus::NotPdf, "only PDF documents are signed");
    }
    const WindowsCert* windows = std::get_if<WindowsCert>(&request.identity);
    std::shared_ptr<const SigningKey> key;
    if (windows == nullptr) {
        key = std::get<std::shared_ptr<const SigningKey>>(request.identity);
        if (!key || !key->key || !key->cert) {
            throw SignError(SignStatus::NoIdentity, "no signing identity");
        }
    } else if (windows->thumbprint.empty()) {
        throw SignError(SignStatus::NoIdentity, "no signing identity");
    }
    if (!doc.allowed(Perm::FillForms)) {
        throw SignError(SignStatus::NotAllowed, "document permissions do not allow signing");
    }
    checkAppearanceFiles(request);
    const bool ltv = request.level == SignLevel::LT || request.level == SignLevel::LTA;
    if (ltv && !request.timestamp) {
        throw SignError(SignStatus::TimestampFailed, "B-LT and B-LTA need a time-stamping service",
                        SaveError::None, TsaStatus::NetworkError);
    }
    const std::shared_ptr<TsaClient> tsa = request.level == SignLevel::B ? nullptr : request.timestamp;
    // Сертификат и ключ хранилища — до записи: нет сертификата или ключа — файл не трогается.
    std::shared_ptr<AcroBatyaSigner> signer;
    X509* cert = nullptr;
    if (windows != nullptr) {
        std::shared_ptr<const CngIdentity> id = openWindowsIdentity(*windows);
        cert = id->cert.get();
        signer =
            std::make_shared<AcroBatyaSigner>(std::move(id), request.digest, tsa, request.withoutTimestamp);
    } else {
        cert = key->cert.get();
        signer = std::make_shared<AcroBatyaSigner>(key, request.digest, tsa, request.withoutTimestamp);
    }
    const std::int64_t t =
        request.signingTime != 0 ? request.signingTime : static_cast<std::int64_t>(std::time(nullptr));
    const std::string name = request.name.empty() ? commonName(X509_get_subject_name(cert)) : request.name;
    AppearanceData data;
    data.name = name;
    data.dn = nameToString(X509_get_subject_name(cert));
    data.reason = request.reason;
    data.location = request.location;
    data.time = t;

    detail::FieldJob job;
    job.placement = request.placement;
    job.lock = request.lock;
    job.time = t;
    // Внешний вид — конструктор appearance.h по рамке виджета.
    job.appearance = [&](fz_context* ctx, fz_rect rect, fz_text_language) {
        try {
            return buildAppearance(ctx, RectF{rect.x0, rect.y0, rect.x1, rect.y1}, request.appearance, data);
        } catch (const MuError&) {
            throw;
        } catch (const std::runtime_error& e) {
            throw SignError(SignStatus::BadAppearance, e.what());
        }
    };
    if (request.subFilter == SubFilter::CadesDetached) {
        job.before = [](fz_context* ctx, pdf_document* pdf) { detail::ensureEsic(ctx, pdf, 2); };
    }
    job.patch = [&](fz_context* ctx, pdf_document*, pdf_obj* v, bool locked) {
        const char* sub = subFilterName(request.subFilter);
        const char* reason = request.reason.c_str();
        const char* location = request.location.c_str();
        const char* contact = request.contact.c_str();
        const char* nameText = name.c_str();
        const int hasReason = request.reason.empty() ? 0 : 1;
        const int hasLocation = request.location.empty() ? 0 : 1;
        const int hasContact = request.contact.empty() ? 0 : 1;
        const char* version = OTTISK_VERSION_STR;
        mu::call(ctx, [&] {
            pdf_dict_put_drop(ctx, v, PDF_NAME(SubFilter), pdf_new_name(ctx, sub));
            if (hasReason != 0) {
                pdf_dict_put_text_string(ctx, v, PDF_NAME(Reason), reason);
            }
            if (hasLocation != 0) {
                pdf_dict_put_text_string(ctx, v, PDF_NAME(Location), location);
            }
            if (hasContact != 0) {
                pdf_dict_put_text_string(ctx, v, PDF_NAME(ContactInfo), contact);
            }
            pdf_dict_put_text_string(ctx, v, PDF_NAME(Name), nameText);
            pdf_obj* build = pdf_dict_put_dict(ctx, v, PDF_NAME(Prop_Build), 2);
            pdf_obj* filter = pdf_dict_put_dict(ctx, build, PDF_NAME(Filter), 1);
            pdf_dict_put(ctx, filter, PDF_NAME(Name), PDF_NAME(Adobe_PPKLite));
            pdf_obj* app = pdf_dict_put_dict(ctx, build, PDF_NAME(App), 2);
            pdf_dict_put_drop(ctx, app, PDF_NAME(Name), pdf_new_name(ctx, "AcroBatya"));
            pdf_dict_put_text_string(ctx, app, PDF_NAME(REx), version);
            // MuPDF пишет FieldMDP /Include [] и без блокировки — пустое правило не нужно.
            if (!locked) {
                pdf_dict_del(ctx, v, PDF_NAME(Reference));
            }
        });
    };

    const detail::FieldOutcome outcome = detail::signField(doc, job, *signer, target);
    SignResult result;
    result.field = outcome.field;
    result.page = outcome.page;
    result.rect = outcome.rect;
    result.visible = outcome.visible;
    result.contentsSize = signer->contentsSize();
    result.timestamped = signer->timestamped();
    result.timestamp = signer->timestampInfo();
    result.timestampError = signer->timestampError();
    result.save = outcome.save;
    spdlog::info("sign: {} signed field \"{}\" ({}, {}, {}, {} bytes)", paths::toUtf8(doc.path()),
                 result.field, subFilterName(request.subFilter), result.timestamped ? "B-T" : "B-B",
                 windows != nullptr ? "Windows store" : "PKCS#12", result.contentsSize);
    result.level = result.timestamped ? SignLevel::T : SignLevel::B;
    if (ltv) {
        extendLevel(doc, request, result);
    }
    return result;
}

}  // namespace ot::sign
