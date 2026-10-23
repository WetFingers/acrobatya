#include "core/annot/annots.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace ot::annot {

namespace {

// Имена /Subtype в порядке AnnotType (= enum pdf_annot_type).
constexpr std::array<std::string_view, kAnnotTypeCount> kAnnotTypeNames = {
    "Text",     "Link",      "FreeText",    "Line",           "Square",    "Circle", "Polygon",
    "PolyLine", "Highlight", "Underline",   "Squiggly",       "StrikeOut", "Redact", "Stamp",
    "Caret",    "Ink",       "Popup",       "FileAttachment", "Sound",     "Movie",  "RichMedia",
    "Widget",   "Screen",    "PrinterMark", "TrapNet",        "Watermark", "3D",     "Projection",
};

pdf_document* annotPdf(const Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        throw std::logic_error("annotations: not a PDF document");
    }
    return pdf;
}

// Одна запись /Annots, прочитанная внутри mu::call (без объектов с деструкторами).
struct AnnotRaw {
    int objnum = 0;
    int flags = 0;
    fz_rect rect{};
    char subtype[64] = {};
};

// Прочитать запись i массива /Annots страницы: рамка — pdf_bound_annot у аннотаций из списков MuPDF,
// иначе (Link, Popup) — /Rect через матрицу страницы. Вызывать внутри mu::call.
void annotReadRaw(fz_context* ctx, pdf_page* page, pdf_obj* annots, int i, fz_matrix ctm, AnnotRaw* out) {
    pdf_obj* obj = pdf_array_get(ctx, annots, i);
    out->objnum = pdf_to_num(ctx, obj);
    out->flags = pdf_dict_get_int(ctx, obj, PDF_NAME(F));
    const char* name = pdf_dict_get_name(ctx, obj, PDF_NAME(Subtype));
    strncpy_s(out->subtype, sizeof(out->subtype), name != nullptr ? name : "", _TRUNCATE);
    pdf_annot* found = nullptr;
    for (pdf_annot* a = pdf_first_annot(ctx, page); a != nullptr && found == nullptr;
         a = pdf_next_annot(ctx, a)) {
        if (pdf_annot_obj(ctx, a) == obj ||
            (out->objnum > 0 && pdf_to_num(ctx, pdf_annot_obj(ctx, a)) == out->objnum)) {
            found = a;
        }
    }
    for (pdf_annot* w = pdf_first_widget(ctx, page); w != nullptr && found == nullptr;
         w = pdf_next_widget(ctx, w)) {
        if (pdf_annot_obj(ctx, w) == obj ||
            (out->objnum > 0 && pdf_to_num(ctx, pdf_annot_obj(ctx, w)) == out->objnum)) {
            found = w;
        }
    }
    if (found != nullptr) {
        out->rect = pdf_bound_annot(ctx, found);
    } else {
        out->rect = fz_transform_rect(pdf_dict_get_rect(ctx, obj, PDF_NAME(Rect)), ctm);
    }
}

AnnotInfo annotFromRaw(int page, const AnnotRaw& raw) {
    AnnotInfo info;
    info.ref = {page, raw.objnum};
    info.subtype = raw.subtype;
    info.type = annotTypeFromName(info.subtype);
    info.flags = raw.flags;
    info.rect = {raw.rect.x0, raw.rect.y0, raw.rect.x1, raw.rect.y1};
    return info;
}

// Страница на время вызова (pdf_page с загруженными списками аннотаций и виджетов).
mu::PageHandle annotLoadPage(const Document& doc, int index) {
    fz_context* ctx = doc.context().get();
    mu::PageHandle page(ctx);
    fz_document* fz = doc.handle();
    mu::call(ctx, [&] { page.reset(fz_load_page(ctx, fz, index)); });
    return page;
}

int annotIndexOf(fz_context* ctx, pdf_obj* annots, int objnum) {
    int index = -1;
    mu::call(ctx, [&] {
        const int n = pdf_array_len(ctx, annots);
        for (int i = 0; i < n && index < 0; ++i) {
            if (pdf_to_num(ctx, pdf_array_get(ctx, annots, i)) == objnum) {
                index = i;
            }
        }
    });
    return index;
}

[[noreturn]] void annotNotFound(const AnnotRef& ref) {
    throw MuError(FZ_ERROR_ARGUMENT,
                  "annotation " + std::to_string(ref.objnum) + " is not on page " + std::to_string(ref.page));
}

}  // namespace

std::string_view annotTypeName(AnnotType type) noexcept {
    const int i = static_cast<int>(type);
    if (i < 0 || i >= kAnnotTypeCount) {
        return {};
    }
    return kAnnotTypeNames[static_cast<std::size_t>(i)];
}

AnnotType annotTypeFromName(std::string_view name) noexcept {
    for (std::size_t i = 0; i < kAnnotTypeNames.size(); ++i) {
        if (kAnnotTypeNames[i] == name) {
            return static_cast<AnnotType>(i);
        }
    }
    return AnnotType::Unknown;
}

std::vector<AnnotInfo> listAnnots(Document& doc, int page) {
    std::vector<AnnotInfo> result;
    if (doc.pdf() == nullptr) {
        return result;
    }
    fz_context* ctx = doc.context().get();
    mu::PageHandle handle = annotLoadPage(doc, page);
    pdf_page* ppage = nullptr;
    pdf_obj* annots = nullptr;
    fz_matrix ctm = fz_identity;
    int n = 0;
    mu::call(ctx, [&] {
        ppage = pdf_page_from_fz_page(ctx, handle);
        if (ppage != nullptr) {
            pdf_page_transform(ctx, ppage, nullptr, &ctm);
            annots = pdf_dict_get(ctx, ppage->obj, PDF_NAME(Annots));
            n = pdf_array_len(ctx, annots);
        }
    });
    result.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        AnnotRaw raw;
        int isDict = 0;
        mu::call(ctx, [&] {
            isDict = pdf_is_dict(ctx, pdf_array_get(ctx, annots, i));
            if (isDict != 0) {
                annotReadRaw(ctx, ppage, annots, i, ctm, &raw);
            }
        });
        if (isDict != 0) {
            result.push_back(annotFromRaw(page, raw));
        }
    }
    return result;
}

std::optional<AnnotInfo> findAnnot(Document& doc, const AnnotRef& ref) {
    if (!ref.valid() || doc.pdf() == nullptr || ref.page >= doc.pageCount()) {
        return std::nullopt;
    }
    for (AnnotInfo& info : listAnnots(doc, ref.page)) {
        if (info.ref == ref) {
            return std::move(info);
        }
    }
    return std::nullopt;
}

std::optional<AnnotInfo> annotAt(Document& doc, int page, PointF point, float tolerance) {
    std::vector<AnnotInfo> list = listAnnots(doc, page);
    // Сначала попадание внутрь (сверху вниз), потом ближайшая в пределах допуска.
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        if (it->type == AnnotType::Popup || it->hiddenOnScreen()) {
            continue;
        }
        if (rectContains(it->rect, point)) {
            return std::move(*it);
        }
    }
    if (tolerance <= 0) {
        return std::nullopt;
    }
    const AnnotInfo* best = nullptr;
    float bestDistance = tolerance;
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        if (it->type == AnnotType::Popup || it->hiddenOnScreen()) {
            continue;
        }
        const float d = rectDistance(it->rect, point);
        if (d <= bestDistance && (best == nullptr || d < bestDistance)) {
            best = &*it;
            bestDistance = d;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    return *best;
}

void setRect(Document& doc, const AnnotRef& ref, const RectF& rect) {
    pdf_document* pdf = annotPdf(doc);
    (void)pdf;
    if (!ref.valid() || ref.page >= doc.pageCount()) {
        annotNotFound(ref);
    }
    if (rect.empty() || !std::isfinite(rect.x0) || !std::isfinite(rect.y0) || !std::isfinite(rect.x1) ||
        !std::isfinite(rect.y1)) {
        throw MuError(FZ_ERROR_ARGUMENT, "annotation rectangle is empty");
    }
    fz_context* ctx = doc.context().get();
    mu::PageHandle handle = annotLoadPage(doc, ref.page);
    pdf_obj* annots = nullptr;
    fz_matrix ctm = fz_identity;
    mu::call(ctx, [&] {
        pdf_page* ppage = pdf_page_from_fz_page(ctx, handle);
        pdf_page_transform(ctx, ppage, nullptr, &ctm);
        annots = pdf_dict_get(ctx, ppage->obj, PDF_NAME(Annots));
    });
    const int index = annotIndexOf(ctx, annots, ref.objnum);
    if (index < 0) {
        annotNotFound(ref);
    }
    Journal::Operation op(doc, "Set annotation rectangle");
    const fz_rect pageRect = fz_make_rect(rect.x0, rect.y0, rect.x1, rect.y1);
    mu::call(ctx, [&] {
        const fz_rect r = fz_transform_rect(pageRect, fz_invert_matrix(ctm));
        pdf_dict_put_rect(ctx, pdf_array_get(ctx, annots, index), PDF_NAME(Rect), r);
    });
    op.invalidate(ref.page);
    op.commit();
}

void remove(Document& doc, const std::vector<AnnotRef>& refs) {
    annotPdf(doc);
    if (refs.empty()) {
        return;
    }
    std::map<int, std::vector<int>> byPage;  // страница → номера объектов
    for (const AnnotRef& ref : refs) {
        if (!ref.valid() || ref.page >= doc.pageCount()) {
            annotNotFound(ref);
        }
        auto& list = byPage[ref.page];
        if (std::find(list.begin(), list.end(), ref.objnum) == list.end()) {
            list.push_back(ref.objnum);
        }
    }
    fz_context* ctx = doc.context().get();
    // Сначала проверка: все на месте — иначе ошибка до правки.
    for (const auto& [pageIndex, objnums] : byPage) {
        mu::PageHandle handle = annotLoadPage(doc, pageIndex);
        pdf_obj* annots = nullptr;
        mu::call(ctx, [&] {
            annots = pdf_dict_get(ctx, pdf_page_from_fz_page(ctx, handle)->obj, PDF_NAME(Annots));
        });
        for (const int objnum : objnums) {
            if (annotIndexOf(ctx, annots, objnum) < 0) {
                annotNotFound({pageIndex, objnum});
            }
        }
    }
    Journal::Operation op(doc, "Delete annotation");
    for (const auto& [pageIndex, objnums] : byPage) {
        mu::PageHandle handle = annotLoadPage(doc, pageIndex);
        pdf_page* ppage = nullptr;
        pdf_obj* annots = nullptr;
        mu::call(ctx, [&] {
            ppage = pdf_page_from_fz_page(ctx, handle);
            annots = pdf_dict_get(ctx, ppage->obj, PDF_NAME(Annots));
        });
        for (const int objnum : objnums) {
            if (annotIndexOf(ctx, annots, objnum) < 0) {
                continue;  // Popup ушёл вместе со своей запиской (pdf_delete_annot) раньше в этом же вызове
            }
            int done = 0;
            mu::call(ctx, [&] {
                pdf_annot* target = nullptr;
                for (pdf_annot* a = pdf_first_annot(ctx, ppage); a != nullptr && target == nullptr;
                     a = pdf_next_annot(ctx, a)) {
                    if (pdf_to_num(ctx, pdf_annot_obj(ctx, a)) == objnum) {
                        target = a;
                    }
                }
                for (pdf_annot* w = pdf_first_widget(ctx, ppage); w != nullptr && target == nullptr;
                     w = pdf_next_widget(ctx, w)) {
                    if (pdf_to_num(ctx, pdf_annot_obj(ctx, w)) == objnum) {
                        target = w;
                    }
                }
                if (target != nullptr) {
                    pdf_delete_annot(ctx, ppage, target);
                    done = 1;
                }
            });
            if (done != 0) {
                continue;
            }
            // Link и Popup: только из /Annots (у родителя Popup — ключ /Popup).
            mu::call(ctx, [&] {
                const int n = pdf_array_len(ctx, annots);
                for (int i = n - 1; i >= 0; --i) {
                    pdf_obj* obj = pdf_array_get(ctx, annots, i);
                    if (pdf_to_num(ctx, obj) != objnum) {
                        continue;
                    }
                    pdf_obj* parent = pdf_dict_get(ctx, obj, PDF_NAME(Parent));
                    if (pdf_is_dict(ctx, parent) &&
                        pdf_to_num(ctx, pdf_dict_get(ctx, parent, PDF_NAME(Popup))) == objnum) {
                        pdf_dict_del(ctx, parent, PDF_NAME(Popup));
                    }
                    pdf_array_delete(ctx, annots, i);
                }
            });
        }
        op.invalidate(pageIndex);
    }
    op.commit();
}

}  // namespace ot::annot
