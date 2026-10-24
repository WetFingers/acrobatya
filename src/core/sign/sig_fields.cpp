#include "core/sign/sig_fields.h"

#include <cstring>

#include "core/doc/document.h"
#include "core/mu/call.h"

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

}  // namespace ot::sign
