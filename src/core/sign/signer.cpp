#include "core/sign/signer.h"

#include <openssl/evp.h>
#include <openssl/x509.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cstring>
#include <ctime>
#include <vector>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/sign/x509_info.h"
#include "core/util/paths.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace ot::sign {

namespace fs = std::filesystem;

using AnnotHandle = mu::Handle<pdf_annot, pdf_drop_annot>;
using SignerHandle = mu::Handle<pdf_pkcs7_signer, pdf_drop_signer>;

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

AcroBatyaSigner::AcroBatyaSigner(std::shared_ptr<const SigningKey> identity, DigestAlgorithm digest)
    : PdfSigner(digest), identity_(std::move(identity)) {}

Bytes AcroBatyaSigner::contents(std::span<const std::uint8_t> digest) {
    try {
        CmsBuilder builder(*identity_, this->digest());
        return builder.buildFromDigest(digest);
    } catch (const OsslError& e) {
        throw SignError(SignStatus::CmsFailed, e.what());
    }
}

std::string AcroBatyaSigner::signingName() const {
    return identity_ && identity_->cert ? commonName(X509_get_subject_name(identity_->cert.get()))
                                        : std::string();
}

// ---------------------------------------------------------------- signDocument

const char* subFilterName(SubFilter subFilter) noexcept {
    return subFilter == SubFilter::Pkcs7Detached ? "adbe.pkcs7.detached" : "ETSI.CAdES.detached";
}

namespace {

// Поле подписи: существующее (номер объекта виджета и страница) или новое (имя и страница).
struct FieldPlan {
    bool existing = false;
    std::string name;
    int page = 0;
    int widget = 0;  // номер объекта виджета существующего поля
};

pdf_obj* formFields(fz_context* ctx, pdf_document* pdf) {
    pdf_obj* fields = nullptr;
    mu::call(ctx, [&] { fields = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/AcroForm/Fields"); });
    return fields;
}

pdf_obj* lookupField(fz_context* ctx, pdf_document* pdf, const std::string& name) {
    pdf_obj* fields = formFields(ctx, pdf);
    pdf_obj* found = nullptr;
    const char* text = name.c_str();
    if (fields != nullptr) {
        mu::call(ctx, [&] { found = pdf_lookup_field(ctx, fields, text); });
    }
    return found;
}

// Страница виджета: /P, иначе поиск в /Annots страниц; −1 — нет.
int widgetPage(Document& doc, pdf_obj* widget) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    int page = -1;
    mu::call(ctx, [&] {
        pdf_obj* p = pdf_dict_get(ctx, widget, PDF_NAME(P));
        if (p != nullptr) {
            page = pdf_lookup_page_number(ctx, pdf, p);
        }
    });
    if (page >= 0) {
        return page;
    }
    const int count = doc.pageCount();
    int target = 0;
    mu::call(ctx, [&] { target = pdf_to_num(ctx, widget); });
    for (int i = 0; i < count; ++i) {
        int hit = 0;
        mu::call(ctx, [&] {
            pdf_obj* annots = pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, pdf, i), PDF_NAME(Annots));
            const int n = pdf_array_len(ctx, annots);
            for (int k = 0; k < n && hit == 0; ++k) {
                hit = pdf_to_num(ctx, pdf_array_get(ctx, annots, k)) == target ? 1 : 0;
            }
        });
        if (hit != 0) {
            return i;
        }
    }
    return -1;
}

FieldPlan planField(Document& doc, const SignaturePlacement& placement) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    FieldPlan plan;
    plan.name = placement.field.empty() ? nextSignatureFieldName(doc) : placement.field;
    pdf_obj* field = placement.field.empty() ? nullptr : lookupField(ctx, pdf, placement.field);
    if (field == nullptr) {
        if (placement.page < 0 || placement.page >= doc.pageCount()) {
            throw SignError(SignStatus::BadPlacement, "no page " + std::to_string(placement.page));
        }
        plan.page = placement.page;
        return plan;
    }
    int isSig = 0;
    int used = 0;
    int ff = 0;
    pdf_obj* widget = nullptr;
    mu::call(ctx, [&] {
        isSig = pdf_dict_get_inheritable(ctx, field, PDF_NAME(FT)) == PDF_NAME(Sig) ? 1 : 0;
        used = pdf_is_dict(ctx, pdf_dict_get_inheritable(ctx, field, PDF_NAME(V)));
        ff = pdf_dict_get_inheritable_int(ctx, field, PDF_NAME(Ff));
        pdf_obj* kids = pdf_dict_get(ctx, field, PDF_NAME(Kids));
        widget = pdf_array_len(ctx, kids) > 0 ? pdf_array_get(ctx, kids, 0) : field;
    });
    if (isSig == 0) {
        throw SignError(SignStatus::NotSignatureField,
                        "field " + placement.field + " is not a signature field");
    }
    if (used != 0) {
        throw SignError(SignStatus::FieldSigned, "field " + placement.field + " is already signed");
    }
    if ((ff & PDF_FIELD_IS_READ_ONLY) != 0) {
        throw SignError(SignStatus::FieldReadOnly, "field " + placement.field + " is read-only");
    }
    plan.existing = true;
    plan.page = widgetPage(doc, widget);
    if (plan.page < 0) {
        throw SignError(SignStatus::BadPlacement, "field " + placement.field + " has no widget on a page");
    }
    mu::call(ctx, [&] { plan.widget = pdf_to_num(ctx, widget); });
    return plan;
}

// Текст справа во внешнем виде по умолчанию: причина, место, дата «2026.10.27 11:00:00 +03'00'».
std::string infoText(const SignRequest& request, std::int64_t t) {
    std::string info;
    const auto line = [&info](const std::string& s) {
        if (!s.empty()) {
            if (!info.empty()) {
                info += '\n';
            }
            info += s;
        }
    };
    line(request.reason);
    line(request.location);
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm local{};
    if (localtime_s(&local, &tt) == 0) {
        char date[40];
        char zone[8];
        if (std::strftime(date, sizeof date, "%Y.%m.%d %H:%M:%S", &local) > 0 &&
            std::strftime(zone, sizeof zone, "%z", &local) == 5) {
            line(std::string(date) + ' ' + std::string(zone, 3) + '\'' + std::string(zone + 3, 2) + '\'');
        }
    }
    return info;
}

// Есть ли у поля правила блокировки (FieldMDP /Reference нужен).
bool hasLock(fz_context* ctx, pdf_obj* widget) {
    int lock = 0;
    mu::call(ctx, [&] { lock = pdf_dict_get(ctx, widget, PDF_NAME(Lock)) != nullptr ? 1 : 0; });
    return lock != 0;
}

// После pdf_signature_set_value MuPDF заводит под каждую следующую правку новую инкрементальную секцию
// («после подписи — новый инкремент», ensure_incremental_xref): патч /V попал бы в секцию после /ByteRange
// подписи. На время патча новые секции запрещены — словарь остаётся в секции подписи.
class SameIncrement {
public:
    explicit SameIncrement(pdf_document* pdf) : pdf_(pdf), saved_(pdf->disallow_new_increments) {
        pdf_->disallow_new_increments = 1;
    }
    ~SameIncrement() { pdf_->disallow_new_increments = saved_; }
    SameIncrement(const SameIncrement&) = delete;
    SameIncrement& operator=(const SameIncrement&) = delete;

private:
    pdf_document* pdf_;
    int saved_;
};

struct Prepared {
    std::string field;
    int page = 0;
    RectF rect;
    bool visible = false;
};

// Одна операция журнала: поле, /Lock, внешний вид, плейсхолдер MuPDF, патч /V и каталога.
Prepared prepare(Document& doc, const SignRequest& request, const FieldPlan& plan, PdfSigner& signer,
                 std::int64_t t) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    Prepared prepared;
    prepared.page = plan.page;

    Journal::Operation op(doc, "Sign document");
    mu::PageHandle page(ctx);
    AnnotHandle widget(ctx);
    mu::call(ctx, [&] { page.reset(fz_load_page(ctx, &pdf->super, plan.page)); });
    pdf_page* ppage = nullptr;
    mu::call(ctx, [&] { ppage = pdf_page_from_fz_page(ctx, page); });
    if (plan.existing) {
        const int objnum = plan.widget;
        mu::call(ctx, [&] {
            for (pdf_annot* w = pdf_first_widget(ctx, ppage); w != nullptr; w = pdf_next_widget(ctx, w)) {
                if (pdf_to_num(ctx, pdf_annot_obj(ctx, w)) == objnum) {
                    widget.reset(pdf_keep_annot(ctx, w));
                    break;
                }
            }
        });
        if (!widget) {
            throw SignError(SignStatus::BadPlacement, "signature widget is not on its page");
        }
    } else {
        std::vector<char> name(plan.name.begin(), plan.name.end());
        name.push_back('\0');
        char* nameBuf = name.data();
        const RectF& r = request.placement.rect;
        const bool visible = !r.empty();
        const fz_rect rect = fz_make_rect(r.x0, r.y0, r.x1, r.y1);
        mu::call(ctx, [&] {
            widget.reset(pdf_create_signature_widget(ctx, ppage, nameBuf));
            pdf_obj* obj = pdf_annot_obj(ctx, widget);
            // MuPDF ставит новому полю /Lock /Action /All — блокировка только по просьбе (sign-15).
            pdf_dict_del(ctx, obj, PDF_NAME(Lock));
            if (visible) {
                pdf_set_annot_rect(ctx, widget, rect);
            } else {
                pdf_dict_put_rect(ctx, obj, PDF_NAME(Rect), fz_make_rect(0, 0, 0, 0));
            }
        });
    }
    pdf_obj* wobj = nullptr;
    mu::call(ctx, [&] { wobj = pdf_annot_obj(ctx, widget); });
    if (request.lock) {
        mu::call(ctx, [&] {
            pdf_obj* lock = pdf_dict_put_dict(ctx, wobj, PDF_NAME(Lock), 2);
            pdf_dict_put_drop(ctx, lock, PDF_NAME(Type), pdf_new_name(ctx, "SigFieldLock"));
            pdf_dict_put(ctx, lock, PDF_NAME(Action), PDF_NAME(All));
        });
    }

    // Внешний вид по умолчанию: имя слева, причина, место и дата справа (конструктор — п. 3).
    fz_rect rect = fz_empty_rect;
    fz_text_language lang = FZ_LANG_UNSET;
    mu::call(ctx, [&] {
        rect = pdf_annot_rect(ctx, widget);
        lang = pdf_annot_language(ctx, widget);
    });
    prepared.visible = !fz_is_empty_rect(rect);
    if (prepared.visible) {
        prepared.rect = RectF{rect.x0, rect.y0, rect.x1, rect.y1};
    }
    const std::string name =
        request.name.empty() ? commonName(X509_get_subject_name(request.identity->cert.get())) : request.name;
    const std::string info = infoText(request, t);
    mu::DisplayListHandle appearance(ctx);
    if (prepared.visible) {
        const char* left = name.c_str();
        const char* right = info.c_str();
        mu::call(ctx, [&] {
            appearance.reset(pdf_signature_appearance_signed(ctx, rect, lang, nullptr, left, right, 0));
        });
    }

    // Каталог — до подписи (после неё MuPDF заводит под правки новую секцию): /Extensions /ESIC для CAdES.
    if (request.subFilter == SubFilter::CadesDetached) {
        mu::call(ctx, [&] {
            pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
            pdf_obj* ext = pdf_dict_gets(ctx, root, "Extensions");
            if (!pdf_is_dict(ctx, ext)) {
                ext = pdf_new_dict(ctx, pdf, 1);
                pdf_dict_puts_drop(ctx, root, "Extensions", ext);
            }
            pdf_obj* esic = pdf_dict_gets(ctx, ext, "ESIC");
            // Массив расширений (PDF 2.0) не трогаем; словарь уровня ниже 2 (1 — DSS) поднимаем.
            if (esic == nullptr ||
                (pdf_is_dict(ctx, esic) && pdf_to_int(ctx, pdf_dict_gets(ctx, esic, "ExtensionLevel")) < 2)) {
                esic = pdf_new_dict(ctx, pdf, 2);
                pdf_dict_puts_drop(ctx, ext, "ESIC", esic);
                pdf_dict_puts_drop(ctx, esic, "BaseVersion", pdf_new_name(ctx, "1.7"));
                pdf_dict_puts_drop(ctx, esic, "ExtensionLevel", pdf_new_int(ctx, 2));
            }
            if (pdf_version(ctx, pdf) < 17) {
                pdf_dict_put_drop(ctx, root, PDF_NAME(Version), pdf_new_name(ctx, "1.7"));
            }
        });
    }

    SignerHandle mu(ctx, signer.newMuSigner());
    mu::call(ctx, [&] { pdf_sign_signature_with_appearance(ctx, widget, mu, t, appearance); });

    // Патч /V до записи: порядок /ByteRange → /Contents → /Filter остаётся (новые ключи — в конце).
    const bool locked = hasLock(ctx, wobj);
    const char* sub = subFilterName(request.subFilter);
    const char* reason = request.reason.c_str();
    const char* location = request.location.c_str();
    const char* contact = request.contact.c_str();
    const char* nameText = name.c_str();
    const int hasReason = request.reason.empty() ? 0 : 1;
    const int hasLocation = request.location.empty() ? 0 : 1;
    const int hasContact = request.contact.empty() ? 0 : 1;
    const char* version = OTTISK_VERSION_STR;
    {
        const SameIncrement same(pdf);
        mu::call(ctx, [&] {
            pdf_obj* v = pdf_dict_get(ctx, wobj, PDF_NAME(V));
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
    }
    char* full = nullptr;
    mu::call(ctx, [&] { full = pdf_load_field_name(ctx, wobj); });
    prepared.field = full != nullptr ? full : plan.name;
    fz_free(ctx, full);
    op.invalidate(plan.page);
    op.commit();
    return prepared;
}

bool needsPreSave(Document& doc) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    int changed = 0;
    int appendable = 0;
    mu::call(ctx, [&] {
        changed = pdf_has_unsaved_changes(ctx, pdf);
        appendable = pdf_can_be_saved_incrementally(ctx, pdf);
    });
    return changed != 0 || appendable == 0 || doc.filePath().empty();
}

[[noreturn]] void rethrowSignerError(const PdfSigner& signer, const SaveReport& report) {
    if (const std::exception_ptr cause = signer.error()) {
        try {
            std::rethrow_exception(cause);
        } catch (const SignError&) {
            throw;
        } catch (const std::exception& e) {
            throw SignError(SignStatus::CmsFailed, e.what());
        }
    }
    throw SignError(SignStatus::SaveFailed, report.message, report.error);
}

}  // namespace

std::string nextSignatureFieldName(Document& doc) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    for (int i = 1;; ++i) {
        std::string name = "Signature" + std::to_string(i);
        if (pdf == nullptr || lookupField(ctx, pdf, name) == nullptr) {
            return name;
        }
    }
}

SignResult signDocument(Document& doc, const SignRequest& request, const fs::path& target) {
    if (doc.pdf() == nullptr) {
        throw SignError(SignStatus::NotPdf, "only PDF documents are signed");
    }
    if (!request.identity || !request.identity->key || !request.identity->cert) {
        throw SignError(SignStatus::NoIdentity, "no signing identity");
    }
    if (!doc.allowed(Perm::FillForms)) {
        throw SignError(SignStatus::NotAllowed, "document permissions do not allow signing");
    }
    const fs::path out = target.empty() ? doc.path() : target;
    if (out.empty()) {
        throw SignError(SignStatus::NoTarget, "the document has no file: choose where to save it");
    }
    FieldPlan plan = planField(doc, request.placement);  // ошибки поля — до записи

    // Несохранённое, новое или восстановленное — сначала обычная запись (подпись дописывается к файлу).
    if (needsPreSave(doc)) {
        const SaveReport pre = Saver::save(doc, out);
        if (!pre.ok()) {
            throw SignError(SignStatus::SaveFailed, "saving before signing: " + pre.message, pre.error);
        }
        plan = planField(doc, request.placement);  // объекты могли перенумероваться
    }

    auto signer = std::make_shared<AcroBatyaSigner>(request.identity, request.digest);
    const std::int64_t t =
        request.signingTime != 0 ? request.signingTime : static_cast<std::int64_t>(std::time(nullptr));
    Prepared prepared;
    try {
        prepared = prepare(doc, request, plan, *signer, t);
    } catch (...) {
        Saver::revert(doc);  // до подписи правок не было — откат к файлу
        throw;
    }
    SaveOptions options;
    options.mode = SaveMode::Incremental;
    const SaveReport report = Saver::save(doc, out, options);
    if (!report.ok()) {
        spdlog::error("sign: writing {} failed: {}", paths::toUtf8(out), report.message);
        if (report.error != SaveError::ReplaceFailed && report.error != SaveError::ReopenFailed) {
            try {
                Saver::revert(doc);
            } catch (const std::exception& e) {
                spdlog::error("sign: revert failed: {}", e.what());
            }
        }
        rethrowSignerError(*signer, report);
    }
    SignResult result;
    result.field = prepared.field;
    result.page = prepared.page;
    result.rect = prepared.rect;
    result.visible = prepared.visible;
    result.contentsSize = signer->contentsSize();
    result.save = report;
    spdlog::info("sign: {} signed field \"{}\" ({}, {} bytes CMS)", paths::toUtf8(out), result.field,
                 subFilterName(request.subFilter), result.contentsSize);
    return result;
}

}  // namespace ot::sign
