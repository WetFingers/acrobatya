#include "core/annot/annots.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <ctime>
#include <limits>
#include <map>
#include <stdexcept>

#include "core/annot/annot_internal.h"
#include "core/annot/comments.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"

namespace ot::annot {

// ---------------------------------------------------------------- внутреннее

namespace detail {

pdf_document* pdfOf(const Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        throw std::logic_error("annotations: not a PDF document");
    }
    return pdf;
}

void notFound(const AnnotRef& ref) {
    throw MuError(FZ_ERROR_ARGUMENT,
                  "annotation " + std::to_string(ref.objnum) + " is not on page " + std::to_string(ref.page));
}

void badArgument(const std::string& message) { throw MuError(FZ_ERROR_ARGUMENT, message); }

std::int64_t now() { return static_cast<std::int64_t>(std::time(nullptr)); }

PageAnnots::PageAnnots(const Document& doc, int index)
    : ctx_(doc.context().get()), pdf_(pdfOf(doc)), handle_(ctx_), index_(index) {
    if (index < 0 || index >= doc.pageCount()) {
        throw MuError(FZ_ERROR_ARGUMENT, "page " + std::to_string(index) + " is out of range");
    }
    fz_document* fz = doc.handle();
    fz_context* ctx = ctx_;
    mu::call(ctx, [&] {
        handle_.reset(fz_load_page(ctx, fz, index));
        page_ = pdf_page_from_fz_page(ctx, handle_);
        if (page_ != nullptr) {
            pdf_page_transform(ctx, page_, nullptr, &ctm_);
        }
    });
    refresh();
}

void PageAnnots::refresh(bool syncAnnots) {
    byNum_.clear();
    if (page_ == nullptr) {
        return;
    }
    fz_context* ctx = ctx_;
    int count = 0;
    mu::call(ctx, [&] {
        if (syncAnnots) {
            pdf_sync_annots(ctx, page_);
        }
        annots_ = pdf_dict_get(ctx, page_->obj, PDF_NAME(Annots));
        for (pdf_annot* a = pdf_first_annot(ctx, page_); a != nullptr; a = pdf_next_annot(ctx, a)) {
            ++count;
        }
        for (pdf_annot* w = pdf_first_widget(ctx, page_); w != nullptr; w = pdf_next_widget(ctx, w)) {
            ++count;
        }
    });
    std::vector<pdf_annot*> all(static_cast<std::size_t>(count), nullptr);
    std::vector<int> nums(static_cast<std::size_t>(count), 0);
    mu::call(ctx, [&] {
        int i = 0;
        for (pdf_annot* a = pdf_first_annot(ctx, page_); a != nullptr && i < count;
             a = pdf_next_annot(ctx, a)) {
            all[static_cast<std::size_t>(i)] = a;
            nums[static_cast<std::size_t>(i)] = pdf_to_num(ctx, pdf_annot_obj(ctx, a));
            ++i;
        }
        for (pdf_annot* w = pdf_first_widget(ctx, page_); w != nullptr && i < count;
             w = pdf_next_widget(ctx, w)) {
            all[static_cast<std::size_t>(i)] = w;
            nums[static_cast<std::size_t>(i)] = pdf_to_num(ctx, pdf_annot_obj(ctx, w));
            ++i;
        }
    });
    byNum_.reserve(all.size());
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (nums[i] > 0) {
            byNum_.emplace(nums[i], all[i]);
        }
    }
}

pdf_annot* PageAnnots::find(int objnum) const {
    const auto it = byNum_.find(objnum);
    return it == byNum_.end() ? nullptr : it->second;
}

int PageAnnots::indexOf(int objnum) const {
    int index = -1;
    if (annots_ == nullptr || objnum <= 0) {
        return index;
    }
    fz_context* ctx = ctx_;
    pdf_obj* annots = annots_;
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

pdf_obj* PageAnnots::object(int objnum) const {
    const int i = indexOf(objnum);
    if (i < 0) {
        return nullptr;
    }
    pdf_obj* obj = nullptr;
    fz_context* ctx = ctx_;
    pdf_obj* annots = annots_;
    mu::call(ctx, [&] { obj = pdf_array_get(ctx, annots, i); });
    return obj;
}

pdf_obj* PageAnnots::ensureAnnots() {
    if (annots_ != nullptr) {
        return annots_;
    }
    fz_context* ctx = ctx_;
    mu::call(ctx, [&] { annots_ = pdf_dict_put_array(ctx, page_->obj, PDF_NAME(Annots), 4); });
    return annots_;
}

namespace {

void transformPointArray(fz_context* ctx, pdf_obj* arr, fz_matrix m) {
    const int n = pdf_array_len(ctx, arr);
    for (int i = 0; i + 1 < n; i += 2) {
        fz_point p = fz_make_point(pdf_array_get_real(ctx, arr, i), pdf_array_get_real(ctx, arr, i + 1));
        p = fz_transform_point(p, m);
        pdf_array_put_real(ctx, arr, i, p.x);
        pdf_array_put_real(ctx, arr, i + 1, p.y);
    }
}

}  // namespace

void transformGeometry(fz_context* ctx, pdf_obj* annot, fz_matrix m) {
    if (pdf_dict_get(ctx, annot, PDF_NAME(Rect)) != nullptr) {
        pdf_dict_put_rect(ctx, annot, PDF_NAME(Rect),
                          fz_transform_rect(pdf_dict_get_rect(ctx, annot, PDF_NAME(Rect)), m));
    }
    pdf_obj* quads = pdf_dict_get(ctx, annot, PDF_NAME(QuadPoints));
    if (pdf_is_array(ctx, quads)) {
        transformPointArray(ctx, quads, m);
    }
    pdf_obj* ink = pdf_dict_get(ctx, annot, PDF_NAME(InkList));
    if (pdf_is_array(ctx, ink)) {
        const int n = pdf_array_len(ctx, ink);
        for (int i = 0; i < n; ++i) {
            transformPointArray(ctx, pdf_array_get(ctx, ink, i), m);
        }
    }
    for (pdf_obj* key : {PDF_NAME(Vertices), PDF_NAME(L), PDF_NAME(CL)}) {
        pdf_obj* arr = pdf_dict_get(ctx, annot, key);
        if (pdf_is_array(ctx, arr)) {
            transformPointArray(ctx, arr, m);
        }
    }
    pdf_obj* popup = pdf_dict_get(ctx, annot, PDF_NAME(Popup));
    if (pdf_is_dict(ctx, popup) && pdf_dict_get(ctx, popup, PDF_NAME(Rect)) != nullptr) {
        pdf_dict_put_rect(ctx, popup, PDF_NAME(Rect),
                          fz_transform_rect(pdf_dict_get_rect(ctx, popup, PDF_NAME(Rect)), m));
    }
}

fz_matrix rawMatrix(fz_matrix ctm, fz_matrix pageSpace) {
    return fz_concat(fz_concat(ctm, pageSpace), fz_invert_matrix(ctm));
}

bool resynthesizeOnResize(AnnotType type) noexcept {
    switch (type) {
        case AnnotType::FreeText:
        case AnnotType::Line:
        case AnnotType::Square:
        case AnnotType::Circle:
        case AnnotType::Polygon:
        case AnnotType::PolyLine:
        case AnnotType::Highlight:
        case AnnotType::Underline:
        case AnnotType::Squiggly:
        case AnnotType::StrikeOut:
        case AnnotType::Caret:
        case AnnotType::Ink:
            return true;
        default:
            return false;
    }
}

}  // namespace detail

namespace {

// Имена /Subtype в порядке AnnotType (= enum pdf_annot_type).
constexpr std::array<std::string_view, kAnnotTypeCount> kAnnotTypeNames = {
    "Text",     "Link",      "FreeText",    "Line",           "Square",    "Circle", "Polygon",
    "PolyLine", "Highlight", "Underline",   "Squiggly",       "StrikeOut", "Redact", "Stamp",
    "Caret",    "Ink",       "Popup",       "FileAttachment", "Sound",     "Movie",  "RichMedia",
    "Widget",   "Screen",    "PrinterMark", "TrapNet",        "Watermark", "3D",     "Projection",
};

// Одна запись /Annots, прочитанная внутри mu::call (без объектов с деструкторами; строки — указатели на
// строки объекта, копируются сразу после вызова).
struct AnnotRaw {
    int objnum = 0;
    int flags = 0;
    fz_rect rect{};
    char subtype[64] = {};
    const char* nm = nullptr;
    const char* author = nullptr;
    const char* contents = nullptr;
    const char* rt = nullptr;
    int colorN = 0;
    float color[4] = {};
    std::int64_t modified = 0;
    int irt = 0;
    int popup = 0;
    int open = 0;
    int hasAp = 0;
};

void annotsReadRaw(fz_context* ctx, pdf_obj* obj, pdf_annot* annot, fz_matrix ctm, AnnotRaw* out) {
    out->objnum = pdf_to_num(ctx, obj);
    out->flags = pdf_dict_get_int(ctx, obj, PDF_NAME(F));
    const char* name = pdf_dict_get_name(ctx, obj, PDF_NAME(Subtype));
    strncpy_s(out->subtype, sizeof(out->subtype), name != nullptr ? name : "", _TRUNCATE);
    if (annot != nullptr) {
        out->rect = pdf_bound_annot(ctx, annot);
    } else {
        out->rect = fz_transform_rect(pdf_dict_get_rect(ctx, obj, PDF_NAME(Rect)), ctm);
    }
    out->nm = pdf_dict_get_text_string(ctx, obj, PDF_NAME(NM));
    out->author = pdf_dict_get_text_string(ctx, obj, PDF_NAME(T));
    out->contents = pdf_dict_get_text_string(ctx, obj, PDF_NAME(Contents));
    out->rt = pdf_dict_get_name(ctx, obj, PDF_NAME(RT));
    pdf_obj* c = pdf_dict_get(ctx, obj, PDF_NAME(C));
    const int n = pdf_array_len(ctx, c);
    if (n == 1 || n == 3 || n == 4) {
        out->colorN = n;
        for (int i = 0; i < n; ++i) {
            out->color[i] = pdf_array_get_real(ctx, c, i);
        }
    }
    out->modified = pdf_dict_get_date(ctx, obj, PDF_NAME(M));
    out->irt = pdf_to_num(ctx, pdf_dict_get(ctx, obj, PDF_NAME(IRT)));
    pdf_obj* popup = pdf_dict_get(ctx, obj, PDF_NAME(Popup));
    out->popup = pdf_to_num(ctx, popup);
    out->open = pdf_is_dict(ctx, popup) ? pdf_dict_get_bool(ctx, popup, PDF_NAME(Open))
                                        : pdf_dict_get_bool(ctx, obj, PDF_NAME(Open));
    out->hasAp = pdf_dict_get(ctx, pdf_dict_get(ctx, obj, PDF_NAME(AP)), PDF_NAME(N)) != nullptr ? 1 : 0;
}

std::string annotsText(const char* s) { return s != nullptr ? std::string(s) : std::string(); }

AnnotInfo annotFromRaw(int page, const AnnotRaw& raw) {
    AnnotInfo info;
    info.ref = {page, raw.objnum, annotsText(raw.nm)};
    info.subtype = raw.subtype;
    info.type = annotTypeFromName(info.subtype);
    info.flags = raw.flags;
    info.rect = {raw.rect.x0, raw.rect.y0, raw.rect.x1, raw.rect.y1};
    info.author = annotsText(raw.author);
    info.contents = annotsText(raw.contents);
    info.replyType = annotsText(raw.rt);
    info.color.n = raw.colorN;
    for (int i = 0; i < raw.colorN; ++i) {
        info.color.c[static_cast<std::size_t>(i)] = raw.color[i];
    }
    info.modified = raw.modified;
    info.inReplyTo = raw.irt;
    info.popup = raw.popup;
    info.open = raw.open != 0;
    info.hasAppearance = raw.hasAp != 0;
    return info;
}

// Геометрия для хит-теста по типу (координаты страницы).
struct HitGeometry {
    std::vector<QuadF> quads;
    std::vector<Stroke> lines;  // штрихи Ink, ломаная, линия, контур многоугольника
    bool closed = false;
    float halfWidth = 0.5f;
};

HitGeometry annotsHitGeometry(fz_context* ctx, pdf_annot* annot, AnnotType type) {
    HitGeometry g;
    int quadCount = 0;
    int strokeCount = 0;
    int vertexCount = 0;
    int hasLine = 0;
    float width = 1;
    mu::call(ctx, [&] {
        if (pdf_annot_has_quad_points(ctx, annot)) {
            quadCount = pdf_annot_quad_point_count(ctx, annot);
        }
        if (pdf_annot_has_ink_list(ctx, annot)) {
            strokeCount = pdf_annot_ink_list_count(ctx, annot);
        }
        if (pdf_annot_has_vertices(ctx, annot)) {
            vertexCount = pdf_annot_vertex_count(ctx, annot);
        }
        hasLine = pdf_annot_has_line(ctx, annot);
        if (pdf_annot_has_border(ctx, annot)) {
            width = pdf_annot_border_width(ctx, annot);
        }
    });
    g.halfWidth = std::max(0.5f, width / 2);
    g.quads.resize(static_cast<std::size_t>(quadCount));
    std::vector<int> strokeSizes(static_cast<std::size_t>(strokeCount), 0);
    mu::call(ctx, [&] {
        for (int i = 0; i < quadCount; ++i) {
            const fz_quad q = pdf_annot_quad_point(ctx, annot, i);
            g.quads[static_cast<std::size_t>(i)] = {
                {q.ul.x, q.ul.y}, {q.ur.x, q.ur.y}, {q.ll.x, q.ll.y}, {q.lr.x, q.lr.y}};
        }
        for (int i = 0; i < strokeCount; ++i) {
            strokeSizes[static_cast<std::size_t>(i)] = pdf_annot_ink_list_stroke_count(ctx, annot, i);
        }
    });
    if (strokeCount > 0) {
        g.lines.resize(static_cast<std::size_t>(strokeCount));
        for (int i = 0; i < strokeCount; ++i) {
            g.lines[static_cast<std::size_t>(i)].resize(
                static_cast<std::size_t>(strokeSizes[static_cast<std::size_t>(i)]));
        }
        mu::call(ctx, [&] {
            for (int i = 0; i < strokeCount; ++i) {
                for (int k = 0; k < strokeSizes[static_cast<std::size_t>(i)]; ++k) {
                    const fz_point p = pdf_annot_ink_list_stroke_vertex(ctx, annot, i, k);
                    g.lines[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] = {p.x, p.y};
                }
            }
        });
    } else if (vertexCount > 0) {
        g.lines.resize(1);
        g.lines[0].resize(static_cast<std::size_t>(vertexCount));
        mu::call(ctx, [&] {
            for (int i = 0; i < vertexCount; ++i) {
                const fz_point p = pdf_annot_vertex(ctx, annot, i);
                g.lines[0][static_cast<std::size_t>(i)] = {p.x, p.y};
            }
        });
        g.closed = type == AnnotType::Polygon;
    } else if (hasLine != 0) {
        fz_point a{};
        fz_point b{};
        mu::call(ctx, [&] { pdf_annot_line(ctx, annot, &a, &b); });
        g.lines.push_back({{a.x, a.y}, {b.x, b.y}});
    }
    return g;
}

// Расстояние от точки до аннотации по её типу (0 — попадание).
float annotsHitDistance(const AnnotInfo& info, const HitGeometry* g, PointF p) {
    if (g == nullptr) {
        return rectDistance(info.rect, p);
    }
    if (!g->quads.empty()) {
        float best = std::numeric_limits<float>::infinity();
        for (const QuadF& q : g->quads) {
            best = std::min(best, distanceToQuad(p, q));
        }
        return best;
    }
    if (!g->lines.empty()) {
        if (g->closed && pointInPolygon(p, g->lines.front())) {
            return 0.0f;
        }
        float best = std::numeric_limits<float>::infinity();
        for (const Stroke& s : g->lines) {
            best = std::min(best, distanceToPolyline(p, s, g->closed));
        }
        return std::max(0.0f, best - g->halfWidth);
    }
    return rectDistance(info.rect, p);
}

bool annotsGeometryHit(AnnotType type) noexcept {
    return isTextMarkup(type) || type == AnnotType::Ink || type == AnnotType::Line ||
           type == AnnotType::PolyLine || type == AnnotType::Polygon;
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

bool isTextMarkup(AnnotType type) noexcept {
    return type == AnnotType::Highlight || type == AnnotType::Underline || type == AnnotType::Squiggly ||
           type == AnnotType::StrikeOut;
}

bool canCreate(AnnotType type) noexcept {
    switch (type) {
        case AnnotType::Text:
        case AnnotType::FreeText:
        case AnnotType::Line:
        case AnnotType::Square:
        case AnnotType::Circle:
        case AnnotType::Polygon:
        case AnnotType::PolyLine:
        case AnnotType::Highlight:
        case AnnotType::Underline:
        case AnnotType::Squiggly:
        case AnnotType::StrikeOut:
        case AnnotType::Stamp:
        case AnnotType::Caret:
        case AnnotType::Ink:
        case AnnotType::FileAttachment:
            return true;
        default:
            return false;
    }
}

bool isComment(AnnotType type) noexcept { return canCreate(type) || type == AnnotType::Redact; }

std::vector<AnnotInfo> listAnnots(Document& doc, int page) {
    std::vector<AnnotInfo> result;
    if (doc.pdf() == nullptr) {
        return result;
    }
    detail::PageAnnots pa(doc, page);
    fz_context* ctx = pa.ctx();
    pdf_obj* annots = pa.annots();
    int n = 0;
    mu::call(ctx, [&] { n = pdf_array_len(ctx, annots); });
    result.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        AnnotRaw raw;
        int isDict = 0;
        int num = 0;
        mu::call(ctx, [&] {
            pdf_obj* obj = pdf_array_get(ctx, annots, i);
            isDict = pdf_is_dict(ctx, obj);
            num = pdf_to_num(ctx, obj);
        });
        if (isDict == 0) {
            continue;
        }
        pdf_annot* annot = pa.find(num);
        const fz_matrix ctm = pa.ctm();
        mu::call(ctx, [&] { annotsReadRaw(ctx, pdf_array_get(ctx, annots, i), annot, ctm, &raw); });
        result.push_back(annotFromRaw(page, raw));
    }
    return result;
}

std::optional<AnnotInfo> findAnnot(Document& doc, const AnnotRef& ref) {
    if (!ref.valid() || doc.pdf() == nullptr || ref.page >= doc.pageCount()) {
        return std::nullopt;
    }
    detail::PageAnnots pa(doc, ref.page);
    pdf_obj* obj = pa.object(ref.objnum);
    if (obj == nullptr) {
        return std::nullopt;
    }
    fz_context* ctx = pa.ctx();
    pdf_annot* annot = pa.find(ref.objnum);
    const fz_matrix ctm = pa.ctm();
    AnnotRaw raw;
    mu::call(ctx, [&] { annotsReadRaw(ctx, obj, annot, ctm, &raw); });
    return annotFromRaw(ref.page, raw);
}

std::optional<AnnotInfo> annotAt(Document& doc, int page, PointF point, float tolerance) {
    std::vector<AnnotInfo> list = listAnnots(doc, page);
    if (list.empty()) {
        return std::nullopt;
    }
    detail::PageAnnots pa(doc, page);
    const float reach = std::max(0.0f, tolerance);
    std::map<int, HitGeometry> geometry;  // objnum → геометрия (только кандидаты по рамке)
    auto distanceOf = [&](const AnnotInfo& info) {
        if (!annotsGeometryHit(info.type)) {
            return rectDistance(info.rect, point);
        }
        // Рамка с запасом на толщину: дальше — точно мимо.
        if (rectDistance(info.rect, point) > reach + 50) {
            return std::numeric_limits<float>::infinity();
        }
        pdf_annot* annot = pa.find(info.ref.objnum);
        if (annot == nullptr) {
            return rectDistance(info.rect, point);
        }
        auto it = geometry.find(info.ref.objnum);
        if (it == geometry.end()) {
            it = geometry.emplace(info.ref.objnum, annotsHitGeometry(pa.ctx(), annot, info.type)).first;
        }
        return annotsHitDistance(info, &it->second, point);
    };
    // Сначала попадание (сверху вниз), потом ближайшая в пределах допуска.
    const AnnotInfo* best = nullptr;
    float bestDistance = std::numeric_limits<float>::infinity();
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        if (it->type == AnnotType::Popup || it->hiddenOnScreen() || isServiceAnnot(*it)) {
            continue;  // окно записки рисует ui; ответы и статусы (5.4) — в панели и окне родителя
        }
        const float d = distanceOf(*it);
        if (d <= 0) {
            return std::move(*it);
        }
        if (d <= reach && d < bestDistance) {
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
    detail::pdfOf(doc);
    if (!ref.valid() || ref.page >= doc.pageCount()) {
        detail::notFound(ref);
    }
    if (rect.empty() || !std::isfinite(rect.x0) || !std::isfinite(rect.y0) || !std::isfinite(rect.x1) ||
        !std::isfinite(rect.y1)) {
        throw MuError(FZ_ERROR_ARGUMENT, "annotation rectangle is empty");
    }
    detail::PageAnnots pa(doc, ref.page);
    pdf_obj* obj = pa.object(ref.objnum);
    if (obj == nullptr) {
        detail::notFound(ref);
    }
    fz_context* ctx = pa.ctx();
    pdf_annot* annot = pa.find(ref.objnum);
    const fz_matrix ctm = pa.ctm();
    fz_rect old{};
    char subtype[64] = {};
    mu::call(ctx, [&] {
        old = annot != nullptr ? pdf_bound_annot(ctx, annot)
                               : fz_transform_rect(pdf_dict_get_rect(ctx, obj, PDF_NAME(Rect)), ctm);
        const char* name = pdf_dict_get_name(ctx, obj, PDF_NAME(Subtype));
        strncpy_s(subtype, sizeof(subtype), name != nullptr ? name : "", _TRUNCATE);
    });
    const AnnotType type = annotTypeFromName(subtype);
    const float oldW = old.x1 - old.x0;
    const float oldH = old.y1 - old.y0;
    const bool sameSize = std::fabs(oldW - rect.width()) < 0.01f && std::fabs(oldH - rect.height()) < 0.01f;
    Journal::Operation op(doc, "Set annotation rectangle");
    if (sameSize || oldW <= 0 || oldH <= 0) {
        // Сдвиг всей геометрии; внешний вид остаётся (вписан в /Rect).
        const fz_matrix m = detail::rawMatrix(ctm, fz_translate(rect.x0 - old.x0, rect.y0 - old.y0));
        if (sameSize) {
            mu::call(ctx, [&] { detail::transformGeometry(ctx, obj, m); });
        } else {
            const fz_rect target = fz_make_rect(rect.x0, rect.y0, rect.x1, rect.y1);
            mu::call(ctx, [&] {
                pdf_dict_put_rect(ctx, obj, PDF_NAME(Rect), fz_transform_rect(target, fz_invert_matrix(ctm)));
            });
        }
    } else {
        // Масштаб: прежняя рамка → новая в координатах страницы.
        fz_matrix s = fz_translate(-old.x0, -old.y0);
        s = fz_concat(s, fz_scale(rect.width() / oldW, rect.height() / oldH));
        s = fz_concat(s, fz_translate(rect.x0, rect.y0));
        const fz_matrix m = detail::rawMatrix(ctm, s);
        const bool resynth = annot != nullptr && detail::resynthesizeOnResize(type);
        mu::call(ctx, [&] { detail::transformGeometry(ctx, obj, m); });
        if (resynth) {
            detail::updateAppearance(doc, pa, ref.objnum, true);  // FreeText — свой вид (freetext.cpp)
        }
    }
    op.invalidate(ref.page);
    op.commit();
}

void remove(Document& doc, const std::vector<AnnotRef>& refs) {
    detail::pdfOf(doc);
    if (refs.empty()) {
        return;
    }
    std::map<int, std::vector<int>> byPage;  // страница → номера объектов
    for (const AnnotRef& ref : refs) {
        if (!ref.valid() || ref.page >= doc.pageCount()) {
            detail::notFound(ref);
        }
        auto& list = byPage[ref.page];
        if (std::find(list.begin(), list.end(), ref.objnum) == list.end()) {
            list.push_back(ref.objnum);
        }
    }
    // Сначала проверка: все на месте — иначе ошибка до правки.
    for (const auto& [pageIndex, objnums] : byPage) {
        const detail::PageAnnots pa(doc, pageIndex);
        for (const int objnum : objnums) {
            if (pa.indexOf(objnum) < 0) {
                detail::notFound({pageIndex, objnum});
            }
        }
    }
    Journal::Operation op(doc, "Delete annotation");
    for (const auto& [pageIndex, objnums] : byPage) {
        detail::PageAnnots pa(doc, pageIndex);
        fz_context* ctx = pa.ctx();
        for (const int objnum : objnums) {
            if (pa.indexOf(objnum) < 0) {
                continue;  // Popup ушёл вместе со своей запиской (pdf_delete_annot) раньше в этом же вызове
            }
            if (pdf_annot* target = pa.find(objnum)) {
                pdf_page* ppage = pa.page();
                mu::call(ctx, [&] { pdf_delete_annot(ctx, ppage, target); });
                pa.refresh();
                continue;
            }
            // Link и Popup: только из /Annots (у родителя Popup — ключ /Popup).
            pdf_obj* annots = pa.annots();
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
            pa.refresh(true);
        }
        op.invalidate(pageIndex);
    }
    op.commit();
}

}  // namespace ot::annot
