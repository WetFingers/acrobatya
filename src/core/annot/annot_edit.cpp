// Свойства, создание и правка аннотаций (план 5.1; эталон 3.4 «Ядро»): чтение всех свойств геттерами MuPDF
// (с проверкой pdf_annot_has_* — неподходящий подтип MuPDF отвергает исключением), создание pdf_create_annot
// + наши сеттеры + pdf_update_annot в одной операции журнала, правка заданных полей, флаги, сдвиг, ластик.
// Строки и массивы для MuPDF готовятся снаружи mu::call (внутри — только вызовы и указатели).
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "core/annot/annot_internal.h"
#include "core/annot/annots.h"
#include "core/annot/defaults.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"

namespace ot::annot {

namespace annot_edit_detail {

using detail::PageAnnots;

// Скаляры и указатели на строки, прочитанные внутри mu::call.
struct PropsRaw {
    int isAnnot = 0;
    char subtype[64] = {};
    fz_rect rect{};
    int flags = 0;
    const char* nm = nullptr;
    const char* contents = nullptr;
    const char* rc = nullptr;
    const char* author = nullptr;
    const char* subject = nullptr;
    const char* icon = nullptr;
    const char* intent = nullptr;
    const char* rt = nullptr;
    const char* font = nullptr;
    int colorN = 0;
    float color[4] = {};
    int icN = 0;
    float ic[4] = {};
    float opacity = 1;
    float borderWidth = 1;
    int borderStyle = 0;
    int dashCount = 0;
    int effect = 0;
    float intensity = 0;
    int quadCount = 0;
    int strokeCount = 0;
    int vertexCount = 0;
    int hasLine = 0;
    fz_point la{};
    fz_point lb{};
    int les = 0;
    int lee = 0;
    float fontSize = 0;
    int daN = 0;
    float da[4] = {};
    int quadding = 0;
    int calloutN = 0;
    fz_point callout[3] = {};
    int hasPopup = 0;
    fz_rect popup{};
    int open = 0;
    int irt = 0;
    std::int64_t created = 0;
    std::int64_t modified = 0;
};

std::string text(const char* s) { return s != nullptr ? std::string(s) : std::string(); }

void readScalars(fz_context* ctx, pdf_obj* obj, pdf_annot* a, fz_matrix ctm, PropsRaw* r) {
    const char* name = pdf_dict_get_name(ctx, obj, PDF_NAME(Subtype));
    strncpy_s(r->subtype, sizeof(r->subtype), name != nullptr ? name : "", _TRUNCATE);
    r->flags = pdf_dict_get_int(ctx, obj, PDF_NAME(F));
    r->nm = pdf_dict_get_text_string(ctx, obj, PDF_NAME(NM));
    r->contents = pdf_dict_get_text_string(ctx, obj, PDF_NAME(Contents));
    r->author = pdf_dict_get_text_string(ctx, obj, PDF_NAME(T));
    r->subject = pdf_dict_get_text_string(ctx, obj, PDF_NAME(Subj));
    r->intent = pdf_dict_get_name(ctx, obj, PDF_NAME(IT));
    r->rt = pdf_dict_get_name(ctx, obj, PDF_NAME(RT));
    r->irt = pdf_to_num(ctx, pdf_dict_get(ctx, obj, PDF_NAME(IRT)));
    r->created = pdf_dict_get_date(ctx, obj, PDF_NAME(CreationDate));
    r->modified = pdf_dict_get_date(ctx, obj, PDF_NAME(M));
    if (a == nullptr) {
        r->rect = fz_transform_rect(pdf_dict_get_rect(ctx, obj, PDF_NAME(Rect)), ctm);
        return;
    }
    r->isAnnot = 1;
    r->rect = pdf_annot_has_rect(ctx, a) ? pdf_annot_rect(ctx, a) : pdf_bound_annot(ctx, a);
    if (pdf_annot_has_rich_contents(ctx, a)) {
        r->rc = pdf_annot_rich_contents(ctx, a);
    }
    if (pdf_annot_has_icon_name(ctx, a)) {
        r->icon = pdf_annot_icon_name(ctx, a);
    }
    pdf_annot_color(ctx, a, &r->colorN, r->color);
    if (pdf_annot_has_interior_color(ctx, a)) {
        pdf_annot_interior_color(ctx, a, &r->icN, r->ic);
    }
    r->opacity = pdf_annot_opacity(ctx, a);
    if (pdf_annot_has_border(ctx, a)) {
        r->borderWidth = pdf_annot_border_width(ctx, a);
        r->borderStyle = static_cast<int>(pdf_annot_border_style(ctx, a));
        r->dashCount = pdf_annot_border_dash_count(ctx, a);
    }
    if (pdf_annot_has_border_effect(ctx, a)) {
        r->effect = static_cast<int>(pdf_annot_border_effect(ctx, a));
        r->intensity = pdf_annot_border_effect_intensity(ctx, a);
    }
    if (pdf_annot_has_quad_points(ctx, a)) {
        r->quadCount = pdf_annot_quad_point_count(ctx, a);
    }
    if (pdf_annot_has_ink_list(ctx, a)) {
        r->strokeCount = pdf_annot_ink_list_count(ctx, a);
    }
    if (pdf_annot_has_vertices(ctx, a)) {
        r->vertexCount = pdf_annot_vertex_count(ctx, a);
    }
    if (pdf_annot_has_line(ctx, a)) {
        r->hasLine = 1;
        pdf_annot_line(ctx, a, &r->la, &r->lb);
    }
    if (pdf_annot_has_line_ending_styles(ctx, a)) {
        enum pdf_line_ending s = PDF_ANNOT_LE_NONE;
        enum pdf_line_ending e = PDF_ANNOT_LE_NONE;
        pdf_annot_line_ending_styles(ctx, a, &s, &e);
        r->les = static_cast<int>(s);
        r->lee = static_cast<int>(e);
    }
    if (pdf_annot_has_default_appearance(ctx, a)) {
        pdf_annot_default_appearance(ctx, a, &r->font, &r->fontSize, &r->daN, r->da);
    }
    if (pdf_annot_has_quadding(ctx, a)) {
        r->quadding = pdf_annot_quadding(ctx, a);
    }
    if (pdf_annot_has_callout(ctx, a)) {
        pdf_annot_callout_line(ctx, a, r->callout, &r->calloutN);
    }
    if (pdf_annot_has_popup(ctx, a) && pdf_dict_get(ctx, obj, PDF_NAME(Popup)) != nullptr) {
        r->hasPopup = 1;
        r->popup = pdf_annot_popup(ctx, a);
    }
    if (pdf_annot_has_open(ctx, a)) {
        r->open = pdf_annot_is_open(ctx, a);
    }
}

AnnotColor colorOf(int n, const float* c) {
    AnnotColor out;
    if (n == 1 || n == 3 || n == 4) {
        out.n = n;
        for (int i = 0; i < n; ++i) {
            out.c[static_cast<std::size_t>(i)] = c[i];
        }
    }
    return out;
}

RectF rectOf(fz_rect r) { return {r.x0, r.y0, r.x1, r.y1}; }

std::optional<AnnotProps> readFrom(PageAnnots& pa, int objnum) {
    pdf_obj* obj = pa.object(objnum);
    if (obj == nullptr) {
        return std::nullopt;
    }
    fz_context* ctx = pa.ctx();
    pdf_annot* a = pa.find(objnum);
    const fz_matrix ctm = pa.ctm();
    PropsRaw r;
    mu::call(ctx, [&] { readScalars(ctx, obj, a, ctm, &r); });
    AnnotProps p;
    p.type = annotTypeFromName(r.subtype);
    p.rect = rectOf(r.rect);
    p.flags = r.flags;
    p.nm = text(r.nm);
    p.contents = text(r.contents);
    p.richContents = text(r.rc);
    p.author = text(r.author);
    p.subject = text(r.subject);
    p.icon = text(r.icon);
    p.intent = text(r.intent);
    p.replyType = text(r.rt);
    p.inReplyTo = r.irt;
    p.created = r.created;
    p.modified = r.modified;
    if (r.isAnnot == 0) {
        return p;
    }
    p.color = colorOf(r.colorN, r.color);
    p.interiorColor = colorOf(r.icN, r.ic);
    p.opacity = r.opacity;
    p.borderWidth = r.borderWidth;
    p.borderStyle = static_cast<BorderStyle>(std::clamp(r.borderStyle, 0, 4));
    p.cloudy = r.effect == PDF_BORDER_EFFECT_CLOUDY;
    p.cloudIntensity = r.intensity;
    p.lineStart = {r.la.x, r.la.y};
    p.lineEnd = {r.lb.x, r.lb.y};
    p.lineStartStyle = static_cast<LineEnding>(std::clamp(r.les, 0, 9));
    p.lineEndStyle = static_cast<LineEnding>(std::clamp(r.lee, 0, 9));
    if (r.font != nullptr) {
        p.fontName = r.font;
        p.fontSize = r.fontSize;
        p.textColor = colorOf(r.daN, r.da);
    }
    p.align = r.quadding;
    for (int i = 0; i < r.calloutN && i < 3; ++i) {
        p.callout.push_back({r.callout[i].x, r.callout[i].y});
    }
    if (r.hasPopup != 0) {
        p.popupRect = rectOf(r.popup);
    }
    p.open = r.open != 0;
    // Массивы: размеры известны — заполнить.
    p.dash.resize(static_cast<std::size_t>(r.dashCount));
    p.quads.resize(static_cast<std::size_t>(r.quadCount));
    p.vertices.resize(static_cast<std::size_t>(r.vertexCount));
    std::vector<int> strokeSizes(static_cast<std::size_t>(r.strokeCount), 0);
    mu::call(ctx, [&] {
        for (int i = 0; i < r.dashCount; ++i) {
            p.dash[static_cast<std::size_t>(i)] = pdf_annot_border_dash_item(ctx, a, i);
        }
        for (int i = 0; i < r.quadCount; ++i) {
            const fz_quad q = pdf_annot_quad_point(ctx, a, i);
            p.quads[static_cast<std::size_t>(i)] = {
                {q.ul.x, q.ul.y}, {q.ur.x, q.ur.y}, {q.ll.x, q.ll.y}, {q.lr.x, q.lr.y}};
        }
        for (int i = 0; i < r.vertexCount; ++i) {
            const fz_point v = pdf_annot_vertex(ctx, a, i);
            p.vertices[static_cast<std::size_t>(i)] = {v.x, v.y};
        }
        for (int i = 0; i < r.strokeCount; ++i) {
            strokeSizes[static_cast<std::size_t>(i)] = pdf_annot_ink_list_stroke_count(ctx, a, i);
        }
    });
    if (r.strokeCount > 0) {
        p.ink.resize(static_cast<std::size_t>(r.strokeCount));
        for (std::size_t i = 0; i < p.ink.size(); ++i) {
            p.ink[i].resize(static_cast<std::size_t>(strokeSizes[i]));
        }
        mu::call(ctx, [&] {
            for (int i = 0; i < r.strokeCount; ++i) {
                for (int k = 0; k < strokeSizes[static_cast<std::size_t>(i)]; ++k) {
                    const fz_point v = pdf_annot_ink_list_stroke_vertex(ctx, a, i, k);
                    p.ink[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] = {v.x, v.y};
                }
            }
        });
    }
    return p;
}

// Массивы для MuPDF, собранные снаружи mu::call.
struct Prepared {
    std::vector<fz_quad> quads;
    std::vector<int> inkCounts;
    std::vector<fz_point> inkPoints;
    std::vector<fz_point> vertices;
    std::vector<fz_point> callout;
};

void prepareQuads(Prepared& prep, const std::vector<QuadF>& quads) {
    prep.quads.clear();
    for (const QuadF& q : quads) {
        prep.quads.push_back({{q.ul.x, q.ul.y}, {q.ur.x, q.ur.y}, {q.ll.x, q.ll.y}, {q.lr.x, q.lr.y}});
    }
}

void prepareInk(Prepared& prep, const std::vector<Stroke>& ink) {
    prep.inkCounts.clear();
    prep.inkPoints.clear();
    for (const Stroke& s : ink) {
        if (s.empty()) {
            continue;
        }
        prep.inkCounts.push_back(static_cast<int>(s.size()));
        for (const PointF p : s) {
            prep.inkPoints.push_back({p.x, p.y});
        }
    }
}

void preparePoints(std::vector<fz_point>& out, const std::vector<PointF>& points) {
    out.clear();
    for (const PointF p : points) {
        out.push_back({p.x, p.y});
    }
}

// Цвет MuPDF; пустой — ключ удаляется.
void setColor(fz_context* ctx, pdf_annot* a, pdf_obj* key, const AnnotColor& c) {
    if (c.empty()) {
        pdf_dict_del(ctx, pdf_annot_obj(ctx, a), key);
        pdf_dirty_annot(ctx, a);
    } else if (key == PDF_NAME(C)) {
        pdf_set_annot_color(ctx, a, c.n, c.c.data());
    } else {
        pdf_set_annot_interior_color(ctx, a, c.n, c.c.data());
    }
}

void setDash(fz_context* ctx, pdf_annot* a, const std::vector<float>& dash, BorderStyle style) {
    pdf_clear_annot_border_dash(ctx, a);
    for (const float d : dash) {
        pdf_add_annot_border_dash_item(ctx, a, d);
    }
    const BorderStyle s =
        !dash.empty() ? BorderStyle::Dashed : (style == BorderStyle::Dashed ? BorderStyle::Solid : style);
    pdf_set_annot_border_style(ctx, a, static_cast<enum pdf_border_style>(s));
}

bool hasGeometryRect(AnnotType type) noexcept {
    return isTextMarkup(type) || type == AnnotType::Ink || type == AnnotType::Line ||
           type == AnnotType::Polygon || type == AnnotType::PolyLine;
}

// Записать свойства новой аннотации (внутри mu::call).
void applyNew(fz_context* ctx, pdf_document* pdf, pdf_annot* a, const AnnotProps& p, const Prepared& prep,
              fz_rect popup, int irt, const char* rt, std::int64_t created, std::int64_t modified,
              const char* nm, fz_buffer* fileData) {
    pdf_obj* obj = pdf_annot_obj(ctx, a);
    if (!hasGeometryRect(p.type) && pdf_annot_has_rect(ctx, a)) {
        pdf_set_annot_rect(ctx, a, fz_make_rect(p.rect.x0, p.rect.y0, p.rect.x1, p.rect.y1));
    }
    if (!prep.quads.empty()) {
        pdf_set_annot_quad_points(ctx, a, static_cast<int>(prep.quads.size()), prep.quads.data());
    }
    if (!prep.inkCounts.empty()) {
        pdf_set_annot_ink_list(ctx, a, static_cast<int>(prep.inkCounts.size()), prep.inkCounts.data(),
                               prep.inkPoints.data());
    }
    if (!prep.vertices.empty()) {
        pdf_set_annot_vertices(ctx, a, static_cast<int>(prep.vertices.size()), prep.vertices.data());
    }
    if (p.type == AnnotType::Line) {
        pdf_set_annot_line(ctx, a, fz_make_point(p.lineStart.x, p.lineStart.y),
                           fz_make_point(p.lineEnd.x, p.lineEnd.y));
    }
    if (pdf_annot_has_line_ending_styles(ctx, a)) {
        pdf_set_annot_line_ending_styles(ctx, a, static_cast<enum pdf_line_ending>(p.lineStartStyle),
                                         static_cast<enum pdf_line_ending>(p.lineEndStyle));
    }
    setColor(ctx, a, PDF_NAME(C), p.color);
    if (pdf_annot_has_interior_color(ctx, a)) {
        setColor(ctx, a, PDF_NAME(IC), p.interiorColor);
    }
    pdf_set_annot_opacity(ctx, a, p.opacity);
    if (pdf_annot_has_border(ctx, a)) {
        pdf_set_annot_border_width(ctx, a, p.borderWidth);
        setDash(ctx, a, p.dash, p.borderStyle);
    }
    if (pdf_annot_has_border_effect(ctx, a)) {
        pdf_set_annot_border_effect(ctx, a, p.cloudy ? PDF_BORDER_EFFECT_CLOUDY : PDF_BORDER_EFFECT_NONE);
        if (p.cloudy) {
            pdf_set_annot_border_effect_intensity(ctx, a, p.cloudIntensity);
        }
    }
    if (pdf_annot_has_icon_name(ctx, a) && !p.icon.empty()) {
        pdf_set_annot_icon_name(ctx, a, p.icon.c_str());
    }
    if (pdf_annot_has_default_appearance(ctx, a)) {
        const AnnotColor& tc = p.textColor.empty() ? AnnotColor::gray(0) : p.textColor;
        pdf_set_annot_default_appearance(ctx, a, p.fontName.empty() ? "Helv" : p.fontName.c_str(),
                                         p.fontSize > 0 ? p.fontSize : 12.0f, tc.n, tc.c.data());
    }
    if (pdf_annot_has_quadding(ctx, a)) {
        pdf_set_annot_quadding(ctx, a, std::clamp(p.align, 0, 2));
    }
    if (pdf_annot_has_callout(ctx, a) && !prep.callout.empty()) {
        fz_point cl[3] = {};
        const int n = std::min(3, static_cast<int>(prep.callout.size()));
        for (int i = 0; i < n; ++i) {
            cl[i] = prep.callout[static_cast<std::size_t>(i)];
        }
        pdf_set_annot_callout_line(ctx, a, cl, n);
    }
    pdf_set_annot_contents(ctx, a, p.contents.c_str());
    if (!p.richContents.empty() && pdf_annot_has_rich_contents(ctx, a)) {
        pdf_set_annot_rich_contents(ctx, a, p.contents.c_str(), p.richContents.c_str());
    }
    if (!p.author.empty()) {
        pdf_dict_put_text_string(ctx, obj, PDF_NAME(T), p.author.c_str());
    }
    pdf_dict_put_text_string(ctx, obj, PDF_NAME(Subj), p.subject.c_str());
    if (!p.intent.empty()) {
        pdf_dict_put_name(ctx, obj, PDF_NAME(IT), p.intent.c_str());
    }
    pdf_dict_put_text_string(ctx, obj, PDF_NAME(NM), nm);
    pdf_set_annot_creation_date(ctx, a, created);
    pdf_set_annot_modification_date(ctx, a, modified);
    pdf_set_annot_flags(ctx, a, p.flags);
    if (irt > 0) {
        pdf_dict_put_drop(ctx, obj, PDF_NAME(IRT), pdf_new_indirect(ctx, pdf, irt, 0));
        pdf_dict_put_name(ctx, obj, PDF_NAME(RT), rt);
    }
    if (pdf_annot_has_popup(ctx, a) && !fz_is_empty_rect(popup)) {
        pdf_set_annot_popup(ctx, a, popup);
        pdf_set_annot_is_open(ctx, a, p.open ? 1 : 0);
    } else if (pdf_annot_has_open(ctx, a) && p.type == AnnotType::Text) {
        pdf_set_annot_is_open(ctx, a, p.open ? 1 : 0);
    }
    if (fileData != nullptr && pdf_annot_has_filespec(ctx, a)) {
        pdf_obj* fs = pdf_add_embedded_file(ctx, pdf, p.fileName.empty() ? "attachment" : p.fileName.c_str(),
                                            "application/octet-stream", fileData, created, modified, 0);
        // MuPDF 1.28.5 открывает в pdf_add_embedded_file операцию «Embed file» и при успехе её не закрывает
        // (pdf-link.c) — закрываем сами, иначе наша операция остаётся незакрытой.
        pdf_end_operation(ctx, pdf);
        pdf_set_annot_filespec(ctx, a, fs);
        pdf_drop_obj(ctx, fs);
    }
    pdf_update_annot(ctx, a);
    // Значок (NoZoom) MuPDF рисует в размере /BBox внешнего вида, а pdfium и Acrobat вписывают его в /Rect
    // (12.5.5): /Rect значка — размером /BBox с тем же центром, и все читалки рисуют одинаково.
    if (p.type == AnnotType::Text || p.type == AnnotType::FileAttachment) {
        pdf_obj* normal = pdf_dict_getp(ctx, obj, "AP/N");
        if (pdf_is_stream(ctx, normal)) {
            const fz_rect bbox = pdf_dict_get_rect(ctx, normal, PDF_NAME(BBox));
            const fz_rect rect = pdf_dict_get_rect(ctx, obj, PDF_NAME(Rect));
            const float w = bbox.x1 - bbox.x0;
            const float h = bbox.y1 - bbox.y0;
            if (w > 0 && h > 0) {
                const float cx = (rect.x0 + rect.x1) / 2;
                const float cy = (rect.y0 + rect.y1) / 2;
                pdf_dict_put_rect(ctx, obj, PDF_NAME(Rect),
                                  fz_make_rect(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2));
            }
        }
    }
}

bool geometryEmpty(const AnnotProps& p) {
    switch (p.type) {
        case AnnotType::Highlight:
        case AnnotType::Underline:
        case AnnotType::Squiggly:
        case AnnotType::StrikeOut:
            return p.quads.empty();
        case AnnotType::Ink:
            return std::none_of(p.ink.begin(), p.ink.end(), [](const Stroke& s) { return !s.empty(); });
        case AnnotType::Polygon:
        case AnnotType::PolyLine:
            return p.vertices.size() < 2;
        case AnnotType::Line:
            return p.lineStart == p.lineEnd;
        default:
            return p.rect.empty();
    }
}

// Рамка окна записки: справа от значка, в пределах страницы.
fz_rect popupFor(const AnnotProps& p, fz_rect page) {
    if (p.hasPopup()) {
        return fz_make_rect(p.popupRect.x0, p.popupRect.y0, p.popupRect.x1, p.popupRect.y1);
    }
    if (p.type != AnnotType::Text) {
        return fz_empty_rect;
    }
    constexpr float kW = 200;
    constexpr float kH = 120;
    float x0 = p.rect.x1 + 10;
    if (x0 + kW > page.x1) {
        x0 = std::max(page.x0, p.rect.x0 - 10 - kW);
    }
    float y0 = std::min(p.rect.y0, page.y1 - kH);
    y0 = std::max(page.y0, y0);
    return fz_make_rect(x0, y0, x0 + kW, y0 + kH);
}

// Создать одну аннотацию на странице (внутри операции вызывающего).
AnnotRef createIn(Document& doc, PageAnnots& pa, const AnnotProps& props, int irt, const char* rt) {
    if (!canCreate(props.type)) {
        detail::badArgument("annotation type " + std::string(annotTypeName(props.type)) +
                            " cannot be created");
    }
    if (geometryEmpty(props)) {
        detail::badArgument("annotation geometry is empty");
    }
    Prepared prep;
    prepareQuads(prep, props.quads);
    prepareInk(prep, props.ink);
    if (props.type == AnnotType::Polygon || props.type == AnnotType::PolyLine) {
        preparePoints(prep.vertices, props.vertices);
    }
    preparePoints(prep.callout, props.callout);
    AnnotProps p = props;
    if (p.subject.empty()) {
        p.subject = defaultSubject(p.type);
    }
    const std::string nm = p.nm.empty() ? newAnnotName() : p.nm;
    const std::int64_t created = p.created > 0 ? p.created : detail::now();
    const std::int64_t modified = p.modified > 0 ? p.modified : created;
    fz_context* ctx = pa.ctx();
    pdf_document* pdf = pa.pdf();
    fz_rect pageRect{};
    mu::call(ctx, [&] { pageRect = fz_bound_page(ctx, reinterpret_cast<fz_page*>(pa.page())); });
    const fz_rect popup = popupFor(p, pageRect);
    mu::BufferHandle file(ctx);
    if (!p.fileData.empty()) {
        mu::call(ctx, [&] {
            file.reset(fz_new_buffer_from_copied_data(
                ctx, reinterpret_cast<const unsigned char*>(p.fileData.data()), p.fileData.size()));
        });
    }
    mu::PdfAnnotHandle annot(ctx);
    int objnum = 0;
    pdf_page* page = pa.page();
    mu::call(ctx, [&] {
        annot.reset(pdf_create_annot(ctx, page, static_cast<enum pdf_annot_type>(p.type)));
        objnum = pdf_to_num(ctx, pdf_annot_obj(ctx, annot));
        applyNew(ctx, pdf, annot, p, prep, popup, irt, rt, created, modified, nm.c_str(), file.get());
    });
    (void)doc;
    pa.refresh();
    return {pa.index(), objnum, nm};
}

void applyPatch(fz_context* ctx, pdf_annot* a, const AnnotPatch& patch, const Prepared& prep,
                std::int64_t modified) {
    pdf_obj* obj = pdf_annot_obj(ctx, a);
    if (patch.contents) {
        pdf_set_annot_contents(ctx, a, patch.contents->c_str());
    }
    if (patch.author) {
        pdf_dict_put_text_string(ctx, obj, PDF_NAME(T), patch.author->c_str());
    }
    if (patch.subject) {
        pdf_dict_put_text_string(ctx, obj, PDF_NAME(Subj), patch.subject->c_str());
    }
    if (patch.icon && pdf_annot_has_icon_name(ctx, a)) {
        pdf_set_annot_icon_name(ctx, a, patch.icon->c_str());
    }
    if (patch.color) {
        setColor(ctx, a, PDF_NAME(C), *patch.color);
    }
    if (patch.interiorColor && pdf_annot_has_interior_color(ctx, a)) {
        setColor(ctx, a, PDF_NAME(IC), *patch.interiorColor);
    }
    if (patch.opacity) {
        pdf_set_annot_opacity(ctx, a, *patch.opacity);
    }
    if (pdf_annot_has_border(ctx, a)) {
        if (patch.borderWidth) {
            pdf_set_annot_border_width(ctx, a, *patch.borderWidth);
        }
        if (patch.dash) {
            setDash(ctx, a, *patch.dash, patch.borderStyle.value_or(BorderStyle::Solid));
        } else if (patch.borderStyle) {
            pdf_set_annot_border_style(ctx, a, static_cast<enum pdf_border_style>(*patch.borderStyle));
        }
    }
    if (patch.cloudy && pdf_annot_has_border_effect(ctx, a)) {
        pdf_set_annot_border_effect(ctx, a,
                                    *patch.cloudy ? PDF_BORDER_EFFECT_CLOUDY : PDF_BORDER_EFFECT_NONE);
        if (*patch.cloudy) {
            pdf_set_annot_border_effect_intensity(ctx, a, 1.0f);
        }
    }
    if (patch.flags) {
        pdf_set_annot_flags(ctx, a, *patch.flags);
    }
    if (patch.popupRect && pdf_annot_has_popup(ctx, a)) {
        const RectF& r = *patch.popupRect;
        pdf_set_annot_popup(ctx, a, fz_make_rect(r.x0, r.y0, r.x1, r.y1));
    }
    if (patch.open && pdf_annot_has_open(ctx, a)) {
        pdf_set_annot_is_open(ctx, a, *patch.open ? 1 : 0);
    }
    if (patch.quads && pdf_annot_has_quad_points(ctx, a) && !prep.quads.empty()) {
        pdf_set_annot_quad_points(ctx, a, static_cast<int>(prep.quads.size()), prep.quads.data());
    }
    if (patch.ink && pdf_annot_has_ink_list(ctx, a) && !prep.inkCounts.empty()) {
        pdf_set_annot_ink_list(ctx, a, static_cast<int>(prep.inkCounts.size()), prep.inkCounts.data(),
                               prep.inkPoints.data());
    }
    if (patch.vertices && pdf_annot_has_vertices(ctx, a) && !prep.vertices.empty()) {
        pdf_set_annot_vertices(ctx, a, static_cast<int>(prep.vertices.size()), prep.vertices.data());
    }
    if ((patch.fontSize || patch.textColor) && pdf_annot_has_default_appearance(ctx, a)) {
        const char* font = nullptr;
        float size = 12;
        int n = 0;
        float color[4] = {};
        pdf_annot_default_appearance(ctx, a, &font, &size, &n, color);
        if (patch.fontSize) {
            size = *patch.fontSize;
        }
        if (patch.textColor && !patch.textColor->empty()) {
            n = patch.textColor->n;
            for (int i = 0; i < n; ++i) {
                color[i] = patch.textColor->c[static_cast<std::size_t>(i)];
            }
        }
        pdf_set_annot_default_appearance(ctx, a, font != nullptr ? font : "Helv", size, n, color);
    }
    if (patch.align && pdf_annot_has_quadding(ctx, a)) {
        pdf_set_annot_quadding(ctx, a, std::clamp(*patch.align, 0, 2));
    }
    pdf_set_annot_modification_date(ctx, a, modified);
    pdf_update_annot(ctx, a);
}

}  // namespace annot_edit_detail

std::string createStepName(AnnotType type) {
    switch (type) {
        case AnnotType::Text:
            return "Add note";
        case AnnotType::FreeText:
            return "Add text box";
        case AnnotType::Line:
            return "Add line";
        case AnnotType::Square:
            return "Add rectangle";
        case AnnotType::Circle:
            return "Add oval";
        case AnnotType::Polygon:
            return "Add polygon";
        case AnnotType::PolyLine:
            return "Add polyline";
        case AnnotType::Highlight:
            return "Add highlight";
        case AnnotType::Underline:
            return "Add underline";
        case AnnotType::Squiggly:
            return "Add squiggly";
        case AnnotType::StrikeOut:
            return "Add strikeout";
        case AnnotType::Stamp:
            return "Add stamp";
        case AnnotType::Caret:
            return "Insert text";
        case AnnotType::Ink:
            return "Add ink";
        case AnnotType::FileAttachment:
            return "Add attachment";
        default:
            return "Add annotation";
    }
}

std::optional<AnnotProps> readProps(Document& doc, const AnnotRef& ref) {
    if (!ref.valid() || doc.pdf() == nullptr || ref.page >= doc.pageCount()) {
        return std::nullopt;
    }
    detail::PageAnnots pa(doc, ref.page);
    return annot_edit_detail::readFrom(pa, ref.objnum);
}

AnnotRef create(Document& doc, int page, const AnnotProps& props, const std::string& operation) {
    detail::pdfOf(doc);
    if (page < 0 || page >= doc.pageCount()) {
        detail::badArgument("page " + std::to_string(page) + " is out of range");
    }
    if (!canCreate(props.type) || annot_edit_detail::geometryEmpty(props)) {
        detail::badArgument("annotation cannot be created: type " + std::string(annotTypeName(props.type)));
    }
    detail::PageAnnots pa(doc, page);
    Journal::Operation op(doc, operation.empty() ? createStepName(props.type) : operation);
    const AnnotRef ref = annot_edit_detail::createIn(doc, pa, props, 0, nullptr);
    op.invalidate(page);
    op.commit();
    return ref;
}

std::vector<AnnotRef> createGroup(Document& doc, int page, const std::vector<AnnotProps>& items,
                                  const std::string& operation) {
    detail::pdfOf(doc);
    if (items.empty()) {
        return {};
    }
    if (page < 0 || page >= doc.pageCount()) {
        detail::badArgument("page " + std::to_string(page) + " is out of range");
    }
    for (const AnnotProps& p : items) {
        if (!canCreate(p.type) || annot_edit_detail::geometryEmpty(p)) {
            detail::badArgument("annotation cannot be created: type " + std::string(annotTypeName(p.type)));
        }
    }
    detail::PageAnnots pa(doc, page);
    Journal::Operation op(doc, operation);
    std::vector<AnnotRef> refs;
    refs.push_back(annot_edit_detail::createIn(doc, pa, items.front(), 0, nullptr));
    for (std::size_t i = 1; i < items.size(); ++i) {
        refs.push_back(annot_edit_detail::createIn(doc, pa, items[i], refs.front().objnum, "Group"));
    }
    op.invalidate(page);
    op.commit();
    return refs;
}

void update(Document& doc, const AnnotRef& ref, const AnnotPatch& patch, const std::string& operation) {
    detail::pdfOf(doc);
    if (!ref.valid() || ref.page >= doc.pageCount()) {
        detail::notFound(ref);
    }
    detail::PageAnnots pa(doc, ref.page);
    pdf_obj* obj = pa.object(ref.objnum);
    if (obj == nullptr) {
        detail::notFound(ref);
    }
    annot_edit_detail::Prepared prep;
    if (patch.quads) {
        annot_edit_detail::prepareQuads(prep, *patch.quads);
    }
    if (patch.ink) {
        annot_edit_detail::prepareInk(prep, *patch.ink);
    }
    if (patch.vertices) {
        annot_edit_detail::preparePoints(prep.vertices, *patch.vertices);
    }
    fz_context* ctx = pa.ctx();
    pdf_annot* a = pa.find(ref.objnum);
    const std::int64_t modified = detail::now();
    Journal::Operation op(doc, operation);
    if (a != nullptr) {
        mu::call(ctx, [&] { annot_edit_detail::applyPatch(ctx, a, patch, prep, modified); });
    } else {
        // Link, Popup: только словарь (флаги и текст).
        mu::call(ctx, [&] {
            if (patch.flags) {
                pdf_dict_put_int(ctx, obj, PDF_NAME(F), *patch.flags);
            }
            if (patch.contents) {
                pdf_dict_put_text_string(ctx, obj, PDF_NAME(Contents), patch.contents->c_str());
            }
            if (patch.open) {
                pdf_dict_put_bool(ctx, obj, PDF_NAME(Open), *patch.open ? 1 : 0);
            }
        });
    }
    op.invalidate(ref.page);
    op.commit();
}

void setFlags(Document& doc, const std::vector<AnnotRef>& refs, int set, int clear,
              const std::string& operation) {
    detail::pdfOf(doc);
    std::map<int, std::vector<int>> byPage;
    for (const AnnotRef& ref : refs) {
        if (!ref.valid() || ref.page >= doc.pageCount()) {
            detail::notFound(ref);
        }
        byPage[ref.page].push_back(ref.objnum);
    }
    Journal::Operation op(doc, operation);
    for (const auto& [page, nums] : byPage) {
        detail::PageAnnots pa(doc, page);
        fz_context* ctx = pa.ctx();
        for (const int num : nums) {
            pdf_obj* obj = pa.object(num);
            if (obj == nullptr) {
                detail::notFound({page, num});
            }
            pdf_annot* a = pa.find(num);
            mu::call(ctx, [&] {
                const int flags = (pdf_dict_get_int(ctx, obj, PDF_NAME(F)) | set) & ~clear;
                if (a != nullptr) {
                    pdf_set_annot_flags(ctx, a, flags);
                    pdf_update_annot(ctx, a);
                } else {
                    pdf_dict_put_int(ctx, obj, PDF_NAME(F), flags);
                }
            });
        }
        op.invalidate(page);
    }
    op.commit();
}

void translate(Document& doc, const std::vector<AnnotRef>& refs, float dx, float dy,
               const std::string& operation) {
    detail::pdfOf(doc);
    std::map<int, std::vector<int>> byPage;
    for (const AnnotRef& ref : refs) {
        if (!ref.valid() || ref.page >= doc.pageCount()) {
            detail::notFound(ref);
        }
        byPage[ref.page].push_back(ref.objnum);
    }
    Journal::Operation op(doc, operation);
    for (const auto& [page, nums] : byPage) {
        detail::PageAnnots pa(doc, page);
        fz_context* ctx = pa.ctx();
        const fz_matrix m = detail::rawMatrix(pa.ctm(), fz_translate(dx, dy));
        for (const int num : nums) {
            pdf_obj* obj = pa.object(num);
            if (obj == nullptr) {
                detail::notFound({page, num});
            }
            mu::call(ctx, [&] { detail::transformGeometry(ctx, obj, m); });
        }
        op.invalidate(page);
    }
    op.commit();
}

bool eraseInk(Document& doc, const AnnotRef& ref, const std::vector<PointF>& path, float radius) {
    detail::pdfOf(doc);
    if (!ref.valid() || ref.page >= doc.pageCount()) {
        detail::notFound(ref);
    }
    detail::PageAnnots pa(doc, ref.page);
    const std::optional<AnnotProps> props = annot_edit_detail::readFrom(pa, ref.objnum);
    if (!props) {
        detail::notFound(ref);
    }
    if (props->type != AnnotType::Ink) {
        detail::badArgument("eraser works on ink annotations only");
    }
    bool changed = false;
    const std::vector<Stroke> left = eraseStrokes(props->ink, path, radius, &changed);
    if (!changed) {
        return false;
    }
    pdf_annot* a = pa.find(ref.objnum);
    if (a == nullptr) {
        detail::notFound(ref);
    }
    fz_context* ctx = pa.ctx();
    Journal::Operation op(doc, "Erase ink");
    if (left.empty()) {
        pdf_page* page = pa.page();
        mu::call(ctx, [&] { pdf_delete_annot(ctx, page, a); });
    } else {
        annot_edit_detail::Prepared prep;
        annot_edit_detail::prepareInk(prep, left);
        const std::int64_t modified = detail::now();
        mu::call(ctx, [&] {
            pdf_set_annot_ink_list(ctx, a, static_cast<int>(prep.inkCounts.size()), prep.inkCounts.data(),
                                   prep.inkPoints.data());
            pdf_set_annot_modification_date(ctx, a, modified);
            pdf_update_annot(ctx, a);
        });
    }
    op.invalidate(ref.page);
    op.commit();
    return true;
}

}  // namespace ot::annot
