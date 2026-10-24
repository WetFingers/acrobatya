#include "core/sign/sign_field.h"

#include <spdlog/spdlog.h>

#include <vector>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/util/paths.h"

namespace ot::sign::detail {

namespace fs = std::filesystem;

namespace {

using AnnotHandle = mu::Handle<pdf_annot, pdf_drop_annot>;
using SignerHandle = mu::Handle<pdf_pkcs7_signer, pdf_drop_signer>;

// Поле подписи: существующее (номер объекта виджета и страница) или новое (имя и страница).
struct FieldPlan {
    bool existing = false;
    std::string name;
    int page = 0;
    int widget = 0;  // номер объекта виджета существующего поля
};

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
    plan.name = placement.field.empty() ? nextFieldName(doc, "Signature") : placement.field;
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

// Одна операция журнала: поле, /Lock, внешний вид, before, плейсхолдер MuPDF, patch.
FieldOutcome prepare(Document& doc, const FieldJob& job, const FieldPlan& plan, PdfSigner& signer) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    FieldOutcome prepared;
    prepared.page = plan.page;

    Journal::Operation op(doc, job.operation);
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
        const RectF& r = job.placement.rect;
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
    if (job.lock) {
        mu::call(ctx, [&] {
            pdf_obj* lock = pdf_dict_put_dict(ctx, wobj, PDF_NAME(Lock), 2);
            pdf_dict_put_drop(ctx, lock, PDF_NAME(Type), pdf_new_name(ctx, "SigFieldLock"));
            pdf_dict_put(ctx, lock, PDF_NAME(Action), PDF_NAME(All));
        });
    }

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
    mu::DisplayListHandle appearance(ctx);
    if (prepared.visible && job.appearance) {
        appearance.reset(job.appearance(ctx, rect, lang));
    }
    // Каталог — до подписи: после неё MuPDF заводит под правки новую секцию.
    if (job.before) {
        job.before(ctx, pdf);
    }

    SignerHandle mu(ctx, signer.newMuSigner());
    const std::int64_t t = job.time;
    mu::call(ctx, [&] { pdf_sign_signature_with_appearance(ctx, widget, mu, t, appearance); });

    // Патч /V до записи: порядок /ByteRange → /Contents → /Filter остаётся (новые ключи — в конце).
    const bool locked = hasLock(ctx, wobj);
    if (job.patch) {
        const SameIncrement same(pdf);
        pdf_obj* v = nullptr;
        mu::call(ctx, [&] { v = pdf_dict_get(ctx, wobj, PDF_NAME(V)); });
        job.patch(ctx, pdf, v, locked);
    }
    char* full = nullptr;
    mu::call(ctx, [&] { full = pdf_load_field_name(ctx, wobj); });
    prepared.field = full != nullptr ? full : plan.name;
    fz_free(ctx, full);
    op.invalidate(plan.page);
    op.commit();
    return prepared;
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

pdf_obj* lookupField(fz_context* ctx, pdf_document* pdf, const std::string& name) {
    pdf_obj* fields = nullptr;
    mu::call(ctx, [&] { fields = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/AcroForm/Fields"); });
    pdf_obj* found = nullptr;
    const char* text = name.c_str();
    if (fields != nullptr) {
        mu::call(ctx, [&] { found = pdf_lookup_field(ctx, fields, text); });
    }
    return found;
}

std::string nextFieldName(Document& doc, const std::string& prefix) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    for (int i = 1;; ++i) {
        std::string name = prefix + std::to_string(i);
        if (pdf == nullptr || lookupField(ctx, pdf, name) == nullptr) {
            return name;
        }
    }
}

void ensureEsic(fz_context* ctx, pdf_document* pdf, int level) {
    mu::call(ctx, [&] {
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
        pdf_obj* ext = pdf_dict_gets(ctx, root, "Extensions");
        if (!pdf_is_dict(ctx, ext)) {
            ext = pdf_new_dict(ctx, pdf, 1);
            pdf_dict_puts_drop(ctx, root, "Extensions", ext);
        }
        pdf_obj* esic = pdf_dict_gets(ctx, ext, "ESIC");
        if (esic == nullptr ||
            (pdf_is_dict(ctx, esic) && pdf_to_int(ctx, pdf_dict_gets(ctx, esic, "ExtensionLevel")) < level)) {
            esic = pdf_new_dict(ctx, pdf, 2);
            pdf_dict_puts_drop(ctx, ext, "ESIC", esic);
            pdf_dict_puts_drop(ctx, esic, "BaseVersion", pdf_new_name(ctx, "1.7"));
            pdf_dict_puts_drop(ctx, esic, "ExtensionLevel", pdf_new_int(ctx, level));
        }
        if (pdf_version(ctx, pdf) < 17) {
            pdf_dict_put_drop(ctx, root, PDF_NAME(Version), pdf_new_name(ctx, "1.7"));
        }
    });
}

FieldOutcome signField(Document& doc, const FieldJob& job, PdfSigner& signer, const fs::path& target) {
    const fs::path out = target.empty() ? doc.path() : target;
    if (out.empty()) {
        throw SignError(SignStatus::NoTarget, "the document has no file: choose where to save it");
    }
    FieldPlan plan = planField(doc, job.placement);  // ошибки поля — до записи

    // Несохранённое, новое или восстановленное — сначала обычная запись (подпись дописывается к файлу).
    if (needsPreSave(doc)) {
        const SaveReport pre = Saver::save(doc, out);
        if (!pre.ok()) {
            throw SignError(SignStatus::SaveFailed, "saving before signing: " + pre.message, pre.error);
        }
        plan = planField(doc, job.placement);  // объекты могли перенумероваться
    }

    FieldOutcome outcome;
    try {
        outcome = prepare(doc, job, plan, signer);
    } catch (...) {
        Saver::revert(doc);  // до подписи правок не было — откат к файлу
        throw;
    }
    SaveOptions options;
    options.mode = SaveMode::Incremental;
    outcome.save = Saver::save(doc, out, options);
    if (!outcome.save.ok()) {
        spdlog::error("sign: writing {} failed: {}", paths::toUtf8(out), outcome.save.message);
        if (outcome.save.error != SaveError::ReplaceFailed && outcome.save.error != SaveError::ReopenFailed) {
            try {
                Saver::revert(doc);
            } catch (const std::exception& e) {
                spdlog::error("sign: revert failed: {}", e.what());
            }
        }
        rethrowSignerError(signer, outcome.save);
    }
    return outcome;
}

}  // namespace ot::sign::detail
