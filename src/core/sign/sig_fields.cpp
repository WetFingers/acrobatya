#include "core/sign/sig_fields.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/revisions.h"
#include "core/mu/call.h"
#include "core/mu/handles.h"
#include "core/sign/sign_field.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace ot::sign {

namespace sig_fields {

struct Scan {
    std::vector<pdf_obj*> fields;
};

// Терминальное поле /Sig: есть /T и нет потомков-полей (у виджетов-потомков /T нет).
void collect(fz_context* ctx, pdf_obj* node, void* arg, pdf_obj** inherited) {
    pdf_obj* ft = inherited[0];
    if (ft == nullptr || !pdf_name_eq(ctx, ft, PDF_NAME(Sig)) ||
        pdf_dict_get(ctx, node, PDF_NAME(T)) == nullptr) {
        return;
    }
    pdf_obj* kids = pdf_dict_get(ctx, node, PDF_NAME(Kids));
    for (int i = 0; i < pdf_array_len(ctx, kids); ++i) {
        if (pdf_dict_get(ctx, pdf_array_get(ctx, kids, i), PDF_NAME(T)) != nullptr) {
            return;
        }
    }
    static_cast<Scan*>(arg)->fields.push_back(node);
}

bool nameIs(fz_context* ctx, pdf_obj* name, const char* text) {
    return pdf_is_name(ctx, name) && std::strcmp(pdf_to_name(ctx, name), text) == 0;
}

}  // namespace sig_fields

std::vector<SignatureField> signatureFields(Document& doc) {
    std::vector<SignatureField> out;
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return out;
    }
    fz_context* ctx = doc.context().get();
    sig_fields::Scan scan;
    pdf_obj* names[2] = {PDF_NAME(FT), nullptr};
    pdf_obj* values[2] = {nullptr, nullptr};
    mu::call(ctx, [&] {
        pdf_obj* fields = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/AcroForm/Fields");
        if (pdf_is_array(ctx, fields)) {
            pdf_walk_tree(ctx, fields, PDF_NAME(Kids), sig_fields::collect, nullptr, &scan, names, values);
        }
    });
    out.reserve(scan.fields.size());
    for (pdf_obj* field : scan.fields) {
        SignatureField f;
        char* name = nullptr;
        int page = -1;
        fz_rect rect = fz_empty_rect;
        int annotFlags = 0;
        int fieldFlags = 0;
        int isSigned = 0;
        int timestamp = 0;
        mu::call(ctx, [&] {
            name = pdf_load_field_name(ctx, field);
            pdf_obj* v = pdf_dict_get(ctx, field, PDF_NAME(V));
            isSigned = pdf_is_dict(ctx, v) ? 1 : 0;
            if (isSigned != 0) {
                timestamp =
                    sig_fields::nameIs(ctx, pdf_dict_get(ctx, v, PDF_NAME(Type)), "DocTimeStamp") ||
                            sig_fields::nameIs(ctx, pdf_dict_get(ctx, v, PDF_NAME(SubFilter)), "ETSI.RFC3161")
                        ? 1
                        : 0;
            }
            fieldFlags = pdf_to_int(ctx, pdf_dict_get_inheritable(ctx, field, PDF_NAME(Ff)));
            pdf_obj* widget = field;
            if (!pdf_name_eq(ctx, pdf_dict_get(ctx, field, PDF_NAME(Subtype)), PDF_NAME(Widget))) {
                pdf_obj* kids = pdf_dict_get(ctx, field, PDF_NAME(Kids));
                widget = pdf_array_len(ctx, kids) > 0 ? pdf_array_get(ctx, kids, 0) : nullptr;
            }
            if (widget != nullptr) {
                pdf_obj* pageObj = pdf_dict_get(ctx, widget, PDF_NAME(P));
                page = pageObj != nullptr ? pdf_lookup_page_number(ctx, pdf, pageObj) : -1;
                if (page < 0) {
                    const int n = pdf_count_pages(ctx, pdf);
                    for (int i = 0; i < n && page < 0; ++i) {
                        if (pdf_array_find(
                                ctx, pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, pdf, i), PDF_NAME(Annots)),
                                widget) >= 0) {
                            page = i;
                        }
                    }
                }
                rect = pdf_dict_get_rect(ctx, widget, PDF_NAME(Rect));
                if (page >= 0) {
                    fz_rect box;
                    fz_matrix ctm;
                    pdf_page_obj_transform(ctx, pdf_lookup_page_obj(ctx, pdf, page), &box, &ctm);
                    rect = fz_transform_rect(rect, ctm);
                }
                annotFlags = pdf_dict_get_int(ctx, widget, PDF_NAME(F));
            }
        });
        f.name = name != nullptr ? name : "";
        fz_free(ctx, name);
        f.page = page;
        f.rect = {rect.x0, rect.y0, rect.x1, rect.y1};
        f.visible = page >= 0 && !fz_is_empty_rect(rect) && rect.x1 - rect.x0 > 0.5f &&
                    rect.y1 - rect.y0 > 0.5f &&
                    (annotFlags & (PDF_ANNOT_IS_HIDDEN | PDF_ANNOT_IS_NO_VIEW)) == 0;
        if (!f.visible) {
            f.rect = {};
        }
        f.isSigned = isSigned != 0;
        f.readOnly = (fieldFlags & PDF_FIELD_IS_READ_ONLY) != 0;
        f.docTimestamp = timestamp != 0;
        out.push_back(std::move(f));
    }
    return out;
}

const SignatureField* emptyFieldAt(const std::vector<SignatureField>& fields, int page, PointF point) {
    for (const SignatureField& f : fields) {
        if (f.page == page && f.visible && !f.isSigned && !f.readOnly && point.x >= f.rect.x0 &&
            point.x <= f.rect.x1 && point.y >= f.rect.y0 && point.y <= f.rect.y1) {
            return &f;
        }
    }
    return nullptr;
}

ClearBlock clearBlock(Document& doc, std::string_view field) {
    const std::vector<SignatureField> fields = signatureFields(doc);
    const auto it =
        std::find_if(fields.begin(), fields.end(), [&](const SignatureField& f) { return f.name == field; });
    if (it == fields.end()) {
        return ClearBlock::NotFound;
    }
    if (!it->isSigned) {
        return ClearBlock::NotSigned;
    }
    const std::vector<revisions::SignatureInfo> signedFields = revisions::signatures(doc);
    std::int64_t end = -1;
    for (const revisions::SignatureInfo& s : signedFields) {
        if (s.field == field) {
            end = s.rangeEnd;
        }
    }
    for (const revisions::SignatureInfo& s : signedFields) {
        if (s.field != field && s.rangeEnd > end) {
            return ClearBlock::LaterSignatures;
        }
    }
    if (it->readOnly) {
        return ClearBlock::ReadOnly;
    }
    return ClearBlock::None;
}

void clearSignature(Document& doc, std::string_view name) {
    const ClearBlock block = clearBlock(doc, name);
    if (block != ClearBlock::None) {
        throw std::runtime_error("signature " + std::string(name) + " cannot be cleared (" +
                                 std::to_string(static_cast<int>(block)) + ")");
    }
    int pageNumber = -1;
    for (const SignatureField& f : signatureFields(doc)) {
        if (f.name == name) {
            pageNumber = f.page;
        }
    }
    if (pageNumber < 0) {
        throw std::runtime_error("signature " + std::string(name) + " has no widget on a page");
    }
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    pdf_obj* field = detail::lookupField(ctx, pdf, std::string(name));
    int widgetNum = 0;
    int valueNum = 0;
    mu::call(ctx, [&] {
        pdf_obj* widget = field;
        if (!pdf_name_eq(ctx, pdf_dict_get(ctx, field, PDF_NAME(Subtype)), PDF_NAME(Widget))) {
            widget = pdf_array_get(ctx, pdf_dict_get(ctx, field, PDF_NAME(Kids)), 0);
        }
        widgetNum = pdf_to_num(ctx, widget);
        valueNum = pdf_to_num(ctx, pdf_dict_get(ctx, field, PDF_NAME(V)));
    });
    Journal::Operation op(doc, "Clear signature");
    mu::PageHandle page(ctx);
    mu::call(ctx, [&] { page.reset(fz_load_page(ctx, &pdf->super, pageNumber)); });
    pdf_annot* widget = nullptr;
    mu::call(ctx, [&] {
        for (pdf_annot* w = pdf_first_widget(ctx, pdf_page_from_fz_page(ctx, page)); w != nullptr;
             w = pdf_next_widget(ctx, w)) {
            if (pdf_to_num(ctx, pdf_annot_obj(ctx, w)) == widgetNum) {
                widget = w;
            }
        }
    });
    if (widget == nullptr) {
        throw std::runtime_error("signature widget is not on its page");
    }
    // То же, что pdf_clear_signature, но внутри нашей операции: MuPDF 1.28 открывает в ней две операции
    // журнала (begin_widget_op и pdf_begin_operation), а закрывает одну — Journal::Operation откатил бы
    // правку.
    fz_rect rect = fz_empty_rect;
    fz_text_language lang = FZ_LANG_UNSET;
    mu::call(ctx, [&] {
        rect = pdf_annot_rect(ctx, widget);
        lang = pdf_annot_language(ctx, widget);
    });
    mu::DisplayListHandle unsignedLook(ctx);
    if (!fz_is_empty_rect(rect)) {
        mu::call(ctx, [&] { unsignedLook.reset(pdf_signature_appearance_unsigned(ctx, rect, lang)); });
    }
    mu::call(ctx, [&] {
        pdf_obj* wobj = pdf_annot_obj(ctx, widget);
        pdf_dirty_annot(ctx, widget);
        const int flags = pdf_dict_get_int(ctx, wobj, PDF_NAME(F)) & ~PDF_ANNOT_IS_LOCKED;
        if (flags != 0) {
            pdf_dict_put_int(ctx, wobj, PDF_NAME(F), flags);
        } else {
            pdf_dict_del(ctx, wobj, PDF_NAME(F));
        }
        pdf_dict_del(ctx, field, PDF_NAME(V));
        pdf_dict_del(ctx, wobj, PDF_NAME(V));
        if (unsignedLook) {
            pdf_set_annot_appearance_from_display_list(ctx, widget, "N", nullptr, fz_identity, unsignedLook);
        } else {
            pdf_dict_del(ctx, wobj, PDF_NAME(AP));
        }
        // Сертифицирующая: /Perms /DocMDP указывал на снятое значение — права документа снимаются тоже.
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
        pdf_obj* perms = pdf_dict_get(ctx, root, PDF_NAME(Perms));
        pdf_obj* docMdp = pdf_dict_get(ctx, perms, PDF_NAME(DocMDP));
        if (docMdp != nullptr && valueNum != 0 && pdf_to_num(ctx, docMdp) == valueNum) {
            pdf_dict_del(ctx, perms, PDF_NAME(DocMDP));
            if (pdf_dict_len(ctx, perms) == 0) {
                pdf_dict_del(ctx, root, PDF_NAME(Perms));
            }
        }
    });
    op.invalidate(pageNumber);
    op.commit();
}

}  // namespace ot::sign
