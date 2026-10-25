#include "core/annot/xfdf.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>

#include "core/annot/annot_internal.h"
#include "core/annot/comments.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"

namespace ot::annot {

namespace {

// Флаги /F — имена XFDF (Adobe) по битам.
constexpr std::array<std::pair<int, std::string_view>, 10> kFlagNames = {{
    {kAnnotInvisible, "invisible"},
    {kAnnotHidden, "hidden"},
    {kAnnotPrint, "print"},
    {kAnnotNoZoom, "nozoom"},
    {kAnnotNoRotate, "norotate"},
    {kAnnotNoView, "noview"},
    {kAnnotReadOnly, "readonly"},
    {kAnnotLocked, "locked"},
    {kAnnotToggleNoView, "togglenoview"},
    {kAnnotLockedContents, "lockedcontents"},
}};

std::string formatFlags(int flags) {
    std::string out;
    for (const auto& [bit, name] : kFlagNames) {
        if ((flags & bit) != 0) {
            if (!out.empty()) {
                out += ',';
            }
            out += name;
        }
    }
    return out;
}

int parseFlags(std::string_view text) {
    int flags = 0;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        std::string_view item = text.substr(0, comma);
        while (!item.empty() && item.front() == ' ') {
            item.remove_prefix(1);
        }
        while (!item.empty() && item.back() == ' ') {
            item.remove_suffix(1);
        }
        for (const auto& [bit, name] : kFlagNames) {
            if (item == name) {
                flags |= bit;
            }
        }
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
    }
    return flags;
}

std::string formatColor(int n, const float* c) {
    float r = 0;
    float g = 0;
    float b = 0;
    if (n == 1) {
        r = g = b = c[0];
    } else if (n == 3) {
        r = c[0];
        g = c[1];
        b = c[2];
    } else if (n == 4) {
        r = (1 - c[0]) * (1 - c[3]);
        g = (1 - c[1]) * (1 - c[3]);
        b = (1 - c[2]) * (1 - c[3]);
    } else {
        return {};
    }
    const auto byte = [](float v) {
        return static_cast<unsigned>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
    };
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out = "#";
    for (const unsigned v : {byte(r), byte(g), byte(b)}) {
        out.push_back(kHex[(v >> 4) & 0xF]);
        out.push_back(kHex[v & 0xF]);
    }
    return out;
}

// "#RRGGBB" → 3 компоненты; не цвет — пусто.
std::vector<float> parseColor(std::string_view text) {
    if (text.size() != 7 || text.front() != '#') {
        return {};
    }
    unsigned v = 0;
    const auto r = std::from_chars(text.data() + 1, text.data() + 7, v, 16);
    if (r.ec != std::errc{}) {
        return {};
    }
    return {static_cast<float>((v >> 16) & 0xFF) / 255.0f, static_cast<float>((v >> 8) & 0xFF) / 255.0f,
            static_cast<float>(v & 0xFF) / 255.0f};
}

const char* borderStyleName(const char* pdfName) {
    if (pdfName == nullptr) {
        return nullptr;
    }
    const std::string_view s(pdfName);
    if (s == "D") {
        return "dash";
    }
    if (s == "B") {
        return "bevelled";
    }
    if (s == "I") {
        return "inset";
    }
    if (s == "U") {
        return "underline";
    }
    if (s == "C") {
        return "cloudy";
    }
    return "solid";
}

const char* borderStylePdf(std::string_view xfdf) {
    if (xfdf == "dash") {
        return "D";
    }
    if (xfdf == "bevelled") {
        return "B";
    }
    if (xfdf == "inset") {
        return "I";
    }
    if (xfdf == "underline") {
        return "U";
    }
    return "S";
}

// Всё, что XFDF берёт из словаря аннотации (читается внутри mu::call; строки — копируются там же).
struct XRaw {
    char subtype[64] = {};
    fz_rect rect{};
    int flags = 0;
    int hasFlags = 0;
    int colorN = 0;
    float color[4] = {};
    int icN = 0;
    float ic[4] = {};
    float opacity = 1;
    int hasOpacity = 0;
    float width = -1;
    char style[8] = {};
    std::string nm, title, subject, contents, rc, date, created, intent, icon, rt, state, stateModel, da, ds,
        irtName, fileName, mime, fileData;
    std::vector<float> dashes, quads, vertices, line, callout, fringe;
    std::vector<std::vector<float>> ink;
    char le[2][32] = {};
    int leN = 0;
    int quadding = -1;
    int rotate = 0;
    int hasPopup = 0;
    fz_rect popupRect{};
    int popupOpen = 0;
    int popupFlags = 0;
    int hasFile = 0;
};

void copyText(fz_context* ctx, pdf_obj* obj, pdf_obj* key, std::string& out) {
    pdf_obj* v = pdf_dict_get(ctx, obj, key);
    if (pdf_is_string(ctx, v)) {
        out = pdf_to_text_string(ctx, v);
    } else if (pdf_is_stream(ctx, v)) {
        // /RC и /DS бывают потоками.
        fz_buffer* b = pdf_load_stream(ctx, v);
        unsigned char* data = nullptr;
        const std::size_t len = fz_buffer_storage(ctx, b, &data);
        out.assign(reinterpret_cast<const char*>(data), len);
        fz_drop_buffer(ctx, b);
    }
}

void copyName(fz_context* ctx, pdf_obj* obj, pdf_obj* key, std::string& out) {
    const char* n = pdf_dict_get_name(ctx, obj, key);
    if (n != nullptr) {
        out = n;
    }
}

std::vector<float> realArray(fz_context* ctx, pdf_obj* arr) {
    std::vector<float> out;
    const int n = pdf_array_len(ctx, arr);
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        out.push_back(pdf_array_get_real(ctx, arr, i));
    }
    return out;
}

void readRaw(fz_context* ctx, pdf_obj* obj, XRaw* r) {
    const char* subtype = pdf_dict_get_name(ctx, obj, PDF_NAME(Subtype));
    strncpy_s(r->subtype, sizeof(r->subtype), subtype != nullptr ? subtype : "", _TRUNCATE);
    r->rect = pdf_dict_get_rect(ctx, obj, PDF_NAME(Rect));
    pdf_obj* f = pdf_dict_get(ctx, obj, PDF_NAME(F));
    r->hasFlags = pdf_is_int(ctx, f) ? 1 : 0;
    r->flags = pdf_to_int(ctx, f);
    for (const auto& [key, n, out] :
         {std::tuple{PDF_NAME(C), &r->colorN, r->color}, std::tuple{PDF_NAME(IC), &r->icN, r->ic}}) {
        pdf_obj* c = pdf_dict_get(ctx, obj, key);
        const int len = pdf_array_len(ctx, c);
        if (len == 1 || len == 3 || len == 4) {
            *n = len;
            for (int i = 0; i < len; ++i) {
                out[i] = pdf_array_get_real(ctx, c, i);
            }
        }
    }
    pdf_obj* ca = pdf_dict_get(ctx, obj, PDF_NAME(CA));
    r->hasOpacity = pdf_is_number(ctx, ca) ? 1 : 0;
    r->opacity = pdf_to_real(ctx, ca);
    pdf_obj* bs = pdf_dict_get(ctx, obj, PDF_NAME(BS));
    if (pdf_is_dict(ctx, bs)) {
        pdf_obj* w = pdf_dict_get(ctx, bs, PDF_NAME(W));
        if (pdf_is_number(ctx, w)) {
            r->width = pdf_to_real(ctx, w);
        }
        const char* s = borderStyleName(pdf_dict_get_name(ctx, bs, PDF_NAME(S)));
        if (s != nullptr) {
            strncpy_s(r->style, sizeof(r->style), s, _TRUNCATE);
        }
        r->dashes = realArray(ctx, pdf_dict_get(ctx, bs, PDF_NAME(D)));
    } else {
        pdf_obj* border = pdf_dict_get(ctx, obj, PDF_NAME(Border));
        if (pdf_array_len(ctx, border) >= 3) {
            r->width = pdf_array_get_real(ctx, border, 2);
        }
    }
    copyText(ctx, obj, PDF_NAME(NM), r->nm);
    copyText(ctx, obj, PDF_NAME(T), r->title);
    copyText(ctx, obj, PDF_NAME(Subj), r->subject);
    copyText(ctx, obj, PDF_NAME(Contents), r->contents);
    copyText(ctx, obj, PDF_NAME(RC), r->rc);
    copyText(ctx, obj, PDF_NAME(M), r->date);
    copyText(ctx, obj, PDF_NAME(CreationDate), r->created);
    copyText(ctx, obj, PDF_NAME(DA), r->da);
    copyText(ctx, obj, PDF_NAME(DS), r->ds);
    copyName(ctx, obj, PDF_NAME(IT), r->intent);
    copyName(ctx, obj, PDF_NAME(Name), r->icon);
    copyName(ctx, obj, PDF_NAME(RT), r->rt);
    pdf_obj* state = pdf_dict_gets(ctx, obj, "State");
    pdf_obj* model = pdf_dict_gets(ctx, obj, "StateModel");
    r->state = pdf_is_name(ctx, state) ? pdf_to_name(ctx, state) : pdf_to_text_string(ctx, state);
    r->stateModel = pdf_is_name(ctx, model) ? pdf_to_name(ctx, model) : pdf_to_text_string(ctx, model);
    pdf_obj* irt = pdf_dict_get(ctx, obj, PDF_NAME(IRT));
    if (pdf_is_dict(ctx, irt)) {
        copyText(ctx, irt, PDF_NAME(NM), r->irtName);
    }
    r->quads = realArray(ctx, pdf_dict_get(ctx, obj, PDF_NAME(QuadPoints)));
    r->vertices = realArray(ctx, pdf_dict_get(ctx, obj, PDF_NAME(Vertices)));
    r->line = realArray(ctx, pdf_dict_get(ctx, obj, PDF_NAME(L)));
    r->callout = realArray(ctx, pdf_dict_get(ctx, obj, PDF_NAME(CL)));
    r->fringe = realArray(ctx, pdf_dict_get(ctx, obj, PDF_NAME(RD)));
    pdf_obj* ink = pdf_dict_get(ctx, obj, PDF_NAME(InkList));
    const int strokes = pdf_array_len(ctx, ink);
    for (int i = 0; i < strokes; ++i) {
        r->ink.push_back(realArray(ctx, pdf_array_get(ctx, ink, i)));
    }
    pdf_obj* le = pdf_dict_get(ctx, obj, PDF_NAME(LE));
    if (pdf_is_name(ctx, le)) {
        strncpy_s(r->le[0], sizeof(r->le[0]), pdf_to_name(ctx, le), _TRUNCATE);
        r->leN = 1;
    } else if (pdf_array_len(ctx, le) == 2) {
        for (int i = 0; i < 2; ++i) {
            const char* n = pdf_to_name(ctx, pdf_array_get(ctx, le, i));
            strncpy_s(r->le[i], sizeof(r->le[i]), n != nullptr ? n : "None", _TRUNCATE);
        }
        r->leN = 2;
    }
    pdf_obj* q = pdf_dict_get(ctx, obj, PDF_NAME(Q));
    r->quadding = pdf_is_int(ctx, q) ? pdf_to_int(ctx, q) : -1;
    r->rotate = pdf_dict_get_int(ctx, obj, PDF_NAME(Rotate));
    pdf_obj* popup = pdf_dict_get(ctx, obj, PDF_NAME(Popup));
    if (pdf_is_dict(ctx, popup)) {
        r->hasPopup = 1;
        r->popupRect = pdf_dict_get_rect(ctx, popup, PDF_NAME(Rect));
        r->popupOpen = pdf_dict_get_bool(ctx, popup, PDF_NAME(Open));
        r->popupFlags = pdf_dict_get_int(ctx, popup, PDF_NAME(F));
    }
    pdf_obj* fs = pdf_dict_get(ctx, obj, PDF_NAME(FS));
    if (pdf_is_dict(ctx, fs) && pdf_is_embedded_file(ctx, fs)) {
        pdf_filespec_params params;
        pdf_get_filespec_params(ctx, fs, &params);
        r->fileName = params.filename != nullptr ? params.filename : "";
        if (params.mimetype != nullptr) {
            r->mime = params.mimetype;
        }
        fz_buffer* b = pdf_load_embedded_file_contents(ctx, fs);
        unsigned char* data = nullptr;
        const std::size_t len = fz_buffer_storage(ctx, b, &data);
        r->fileData.assign(reinterpret_cast<const char*>(data), len);
        fz_drop_buffer(ctx, b);
        r->hasFile = 1;
    }
}

std::vector<PointF> toPoints(const std::vector<float>& v) {
    std::vector<PointF> out;
    for (std::size_t i = 0; i + 1 < v.size(); i += 2) {
        out.push_back({v[i], v[i + 1]});
    }
    return out;
}

std::vector<float> fromPoints(const std::vector<PointF>& points) {
    std::vector<float> out;
    for (const PointF& p : points) {
        out.push_back(p.x);
        out.push_back(p.y);
    }
    return out;
}

std::string pointsText(const std::vector<PointF>& points) {
    std::string out;
    for (const PointF& p : points) {
        if (!out.empty()) {
            out += ';';
        }
        out += formatNumbers({p.x, p.y});
    }
    return out;
}

XfdfAnnot toXfdf(int page, const XRaw& r) {
    XfdfAnnot a;
    a.element = xfdfElement(annotTypeFromName(r.subtype));
    const AnnotType type = annotTypeFromName(r.subtype);
    a.set("page", std::to_string(page));
    a.set("rect", formatNumbers({r.rect.x0, r.rect.y0, r.rect.x1, r.rect.y1}));
    if (r.hasFlags != 0) {
        a.set("flags", formatFlags(r.flags));
    }
    if (!r.nm.empty()) {
        a.set("name", r.nm);
    }
    if (!r.title.empty()) {
        a.set("title", r.title);
    }
    if (!r.subject.empty()) {
        a.set("subject", r.subject);
    }
    if (!r.date.empty()) {
        a.set("date", r.date);
    }
    if (!r.created.empty()) {
        a.set("creationdate", r.created);
    }
    if (const std::string c = formatColor(r.colorN, r.color); !c.empty()) {
        a.set("color", c);
    }
    if (const std::string c = formatColor(r.icN, r.ic); !c.empty()) {
        a.set("interior-color", c);
    }
    if (r.hasOpacity != 0) {
        a.set("opacity", formatNumbers({r.opacity}));
    }
    if (r.width >= 0) {
        a.set("width", formatNumbers({r.width}));
    }
    if (r.style[0] != 0) {
        a.set("style", r.style);
    }
    if (!r.dashes.empty()) {
        a.set("dashes", formatNumbers(r.dashes));
    }
    if (!r.intent.empty()) {
        a.set("intent", r.intent);
    }
    if (!r.icon.empty()) {
        a.set("icon", r.icon);
    }
    if (r.rotate != 0) {
        a.set("rotation", std::to_string(r.rotate));
    }
    if (!r.quads.empty()) {
        a.set("coords", formatNumbers(r.quads));
    }
    if (r.line.size() == 4) {
        a.set("start", formatNumbers({r.line[0], r.line[1]}));
        a.set("end", formatNumbers({r.line[2], r.line[3]}));
    }
    if (r.leN == 2) {
        a.set("head", r.le[0]);
        a.set("tail", r.le[1]);
    } else if (r.leN == 1) {
        a.set("head", r.le[0]);
    }
    if (!r.callout.empty()) {
        a.set("callout", formatNumbers(r.callout));
    }
    if (r.fringe.size() == 4) {
        a.set("fringe", formatNumbers(r.fringe));
    }
    if (r.quadding >= 0) {
        a.set("justification", r.quadding == 1 ? "centered" : (r.quadding == 2 ? "right" : "left"));
    }
    if (!r.irtName.empty()) {
        a.set("inreplyto", r.irtName);
        a.set("replyType", r.rt == "Group" ? "group" : "reply");
    }
    if (!r.state.empty()) {
        a.set("state", r.state);
    }
    if (!r.stateModel.empty()) {
        a.set("statemodel", r.stateModel);
    }
    a.contents = r.contents;
    a.richText = r.rc;
    a.defaultAppearance = r.da;
    a.defaultStyle = r.ds;
    for (const std::vector<float>& stroke : r.ink) {
        a.ink.push_back(toPoints(stroke));
    }
    if (type == AnnotType::Polygon || type == AnnotType::PolyLine) {
        a.vertices = toPoints(r.vertices);
    } else if (!r.vertices.empty()) {
        a.vertices = toPoints(r.vertices);
    }
    if (r.hasPopup != 0) {
        a.hasPopup = true;
        a.popupRect = {r.popupRect.x0, r.popupRect.y0, r.popupRect.x1, r.popupRect.y1};
        a.popupOpen = r.popupOpen != 0;
        a.popupFlags = r.popupFlags;
    }
    if (r.hasFile != 0) {
        a.set("file", r.fileName);
        if (!r.mime.empty()) {
            a.set("mimetype", r.mime);
        }
        a.fileData = r.fileData;
        a.hasFileData = true;
    }
    return a;
}

// Значения нового словаря, подготовленные вне mu::call.
struct XNew {
    AnnotType type = AnnotType::Unknown;
    int page = 0;
    std::string nm;
    std::vector<float> rect, color, interior, dashes, quads, vertices, line, callout, fringe;
    std::vector<std::vector<float>> ink;
    bool hasFlags = false;
    int flags = 0;
    bool hasOpacity = false;
    float opacity = 1;
    bool hasWidth = false;
    float width = 1;
    std::string style, title, subject, contents, rc, date, created, intent, icon, da, ds, state, stateModel,
        rt, head, tail, fileName, mime;
    int quadding = -1;
    int rotate = 0;
};

float numberOr(std::string_view text, float fallback) {
    const std::vector<float> v = parseNumbers(text);
    return v.empty() ? fallback : v.front();
}

XNew fromXfdf(const XfdfAnnot& a, AnnotType type) {
    XNew n;
    n.type = type;
    n.page = static_cast<int>(numberOr(a.attribute("page"), 0));
    n.nm = a.attribute("name");
    n.rect = parseNumbers(a.attribute("rect"));
    n.color = parseColor(a.attribute("color"));
    n.interior = parseColor(a.attribute("interior-color"));
    n.hasFlags = a.has("flags");
    n.flags = parseFlags(a.attribute("flags"));
    n.hasOpacity = a.has("opacity");
    n.opacity = numberOr(a.attribute("opacity"), 1);
    n.hasWidth = a.has("width");
    n.width = numberOr(a.attribute("width"), 1);
    n.style = a.attribute("style");
    n.dashes = parseNumbers(a.attribute("dashes"));
    n.title = a.attribute("title");
    n.subject = a.attribute("subject");
    n.rc = a.richText;
    n.contents = a.contents;
    if (n.contents.empty() && !n.rc.empty()) {
        n.contents = plainText(parseRichContents(n.rc));  // Acrobat пишет только contents-richtext
    }
    n.date = a.attribute("date");
    n.created = a.attribute("creationdate");
    n.intent = a.attribute("intent");
    n.icon = a.attribute("icon");
    n.da = a.defaultAppearance;
    n.ds = a.defaultStyle;
    n.state = a.attribute("state");
    n.stateModel = a.attribute("statemodel");
    n.rt = a.attribute("replyType");
    n.quads = parseNumbers(a.attribute("coords"));
    const std::vector<float> start = parseNumbers(a.attribute("start"));
    const std::vector<float> end = parseNumbers(a.attribute("end"));
    if (start.size() == 2 && end.size() == 2) {
        n.line = {start[0], start[1], end[0], end[1]};
    }
    n.head = a.attribute("head");
    n.tail = a.attribute("tail");
    n.callout = parseNumbers(a.attribute("callout"));
    n.fringe = parseNumbers(a.attribute("fringe"));
    n.vertices = fromPoints(a.vertices);
    for (const std::vector<PointF>& stroke : a.ink) {
        n.ink.push_back(fromPoints(stroke));
    }
    const std::string just = a.attribute("justification");
    n.quadding = just == "centered" ? 1 : (just == "right" ? 2 : (just == "left" ? 0 : -1));
    n.rotate = static_cast<int>(numberOr(a.attribute("rotation"), 0));
    n.fileName = a.attribute("file");
    n.mime = a.attribute("mimetype");
    if ((n.type == AnnotType::FreeText || n.type == AnnotType::Redact) && n.da.empty()) {
        n.da = "/Helv 12 Tf 0 g";  // у Redact /DA обязателен для строгих проверок (ISO 32000-2, табл. 195)
    }
    return n;
}

pdf_obj* numberArray(fz_context* ctx, pdf_document* pdf, const std::vector<float>& v) {
    pdf_obj* arr = pdf_new_array(ctx, pdf, static_cast<int>(v.size()));
    for (const float f : v) {
        pdf_array_push_real(ctx, arr, f);
    }
    return arr;
}

void putText(fz_context* ctx, pdf_obj* dict, pdf_obj* key, const std::string& value) {
    if (!value.empty()) {
        pdf_dict_put_text_string(ctx, dict, key, value.c_str());
    }
}

void putName(fz_context* ctx, pdf_obj* dict, pdf_obj* key, const std::string& value) {
    if (!value.empty()) {
        pdf_dict_put_name(ctx, dict, key, value.c_str());
    }
}

// Новый словарь аннотации косвенным объектом документа (внутри mu::call): новая ссылка на него.
pdf_obj* buildDict(fz_context* ctx, pdf_document* pdf, const XNew& n, fz_buffer* file) {
    pdf_obj* d = pdf_add_new_dict(ctx, pdf, 32);
    {
        pdf_dict_put(ctx, d, PDF_NAME(Type), PDF_NAME(Annot));
        pdf_dict_put_name(ctx, d, PDF_NAME(Subtype), annotTypeName(n.type).data());  // строковые литералы
        if (n.rect.size() == 4) {
            pdf_dict_put_rect(ctx, d, PDF_NAME(Rect),
                              fz_make_rect(n.rect[0], n.rect[1], n.rect[2], n.rect[3]));
        }
        if (n.hasFlags) {
            pdf_dict_put_int(ctx, d, PDF_NAME(F), n.flags);
        }
        putText(ctx, d, PDF_NAME(NM), n.nm);
        putText(ctx, d, PDF_NAME(T), n.title);
        putText(ctx, d, PDF_NAME(Subj), n.subject);
        putText(ctx, d, PDF_NAME(Contents), n.contents);
        putText(ctx, d, PDF_NAME(RC), n.rc);
        putText(ctx, d, PDF_NAME(M), n.date);
        putText(ctx, d, PDF_NAME(CreationDate), n.created);
        putText(ctx, d, PDF_NAME(DA), n.da);
        putText(ctx, d, PDF_NAME(DS), n.ds);
        putName(ctx, d, PDF_NAME(IT), n.intent);
        putName(ctx, d, PDF_NAME(Name), n.icon);
        if (!n.color.empty()) {
            pdf_dict_put_drop(ctx, d, PDF_NAME(C), numberArray(ctx, pdf, n.color));
        }
        if (!n.interior.empty()) {
            pdf_dict_put_drop(ctx, d, PDF_NAME(IC), numberArray(ctx, pdf, n.interior));
        }
        if (n.hasOpacity) {
            pdf_dict_put_real(ctx, d, PDF_NAME(CA), n.opacity);
        }
        if (n.hasWidth || !n.style.empty() || !n.dashes.empty()) {
            pdf_obj* bs = pdf_dict_put_dict(ctx, d, PDF_NAME(BS), 3);
            pdf_dict_put_real(ctx, bs, PDF_NAME(W), n.width);
            if (n.style == "cloudy") {
                pdf_obj* be = pdf_dict_put_dict(ctx, d, PDF_NAME(BE), 2);
                pdf_dict_put(ctx, be, PDF_NAME(S), PDF_NAME(C));
                pdf_dict_put_name(ctx, bs, PDF_NAME(S), "S");
            } else if (!n.style.empty()) {
                pdf_dict_put_name(ctx, bs, PDF_NAME(S), borderStylePdf(n.style));
            }
            if (!n.dashes.empty()) {
                pdf_dict_put_drop(ctx, bs, PDF_NAME(D), numberArray(ctx, pdf, n.dashes));
            }
        }
        if (!n.quads.empty()) {
            pdf_dict_put_drop(ctx, d, PDF_NAME(QuadPoints), numberArray(ctx, pdf, n.quads));
        }
        if (!n.vertices.empty()) {
            pdf_dict_put_drop(ctx, d, PDF_NAME(Vertices), numberArray(ctx, pdf, n.vertices));
        }
        if (!n.ink.empty()) {
            pdf_obj* list = pdf_dict_put_array(ctx, d, PDF_NAME(InkList), static_cast<int>(n.ink.size()));
            for (const std::vector<float>& stroke : n.ink) {
                pdf_array_push_drop(ctx, list, numberArray(ctx, pdf, stroke));
            }
        }
        if (n.line.size() == 4) {
            pdf_dict_put_drop(ctx, d, PDF_NAME(L), numberArray(ctx, pdf, n.line));
        }
        if (n.type == AnnotType::FreeText) {
            if (!n.head.empty()) {
                pdf_dict_put_name(ctx, d, PDF_NAME(LE), n.head.c_str());
            }
        } else if (!n.head.empty() || !n.tail.empty()) {
            pdf_obj* le = pdf_dict_put_array(ctx, d, PDF_NAME(LE), 2);
            pdf_array_push_name(ctx, le, n.head.empty() ? "None" : n.head.c_str());
            pdf_array_push_name(ctx, le, n.tail.empty() ? "None" : n.tail.c_str());
        }
        if (!n.callout.empty()) {
            pdf_dict_put_drop(ctx, d, PDF_NAME(CL), numberArray(ctx, pdf, n.callout));
        }
        if (n.fringe.size() == 4) {
            pdf_dict_put_drop(ctx, d, PDF_NAME(RD), numberArray(ctx, pdf, n.fringe));
        }
        if (n.quadding >= 0) {
            pdf_dict_put_int(ctx, d, PDF_NAME(Q), n.quadding);
        }
        if (n.rotate != 0) {
            pdf_dict_put_int(ctx, d, PDF_NAME(Rotate), n.rotate);
        }
        if (!n.state.empty()) {
            pdf_dict_puts_drop(ctx, d, "State", pdf_new_text_string(ctx, n.state.c_str()));
        }
        if (!n.stateModel.empty()) {
            pdf_dict_puts_drop(ctx, d, "StateModel", pdf_new_text_string(ctx, n.stateModel.c_str()));
        }
        if (n.rt == "group") {
            pdf_dict_put(ctx, d, PDF_NAME(RT), PDF_NAME(Group));
        }
        if (file != nullptr) {
            pdf_obj* fs = pdf_add_embedded_file(
                ctx, pdf, n.fileName.empty() ? "attachment" : n.fileName.c_str(),
                n.mime.empty() ? "application/octet-stream" : n.mime.c_str(), file, 0, 0, 0);
            // MuPDF 1.28.5 не закрывает свою операцию «Embed file» (pdf-link.c) — закрываем сами.
            pdf_end_operation(ctx, pdf);
            pdf_dict_put_drop(ctx, d, PDF_NAME(FS), fs);
        }
    }
    return d;
}

}  // namespace

// ---------------------------------------------------------------- модель

std::string XfdfAnnot::attribute(std::string_view name) const {
    for (const auto& [k, v] : attributes) {
        if (k == name) {
            return v;
        }
    }
    return {};
}

bool XfdfAnnot::has(std::string_view name) const {
    return std::any_of(attributes.begin(), attributes.end(),
                       [&](const auto& kv) { return kv.first == name; });
}

void XfdfAnnot::set(std::string name, std::string value) {
    for (auto& [k, v] : attributes) {
        if (k == name) {
            v = std::move(value);
            return;
        }
    }
    attributes.emplace_back(std::move(name), std::move(value));
}

std::string xfdfElement(AnnotType type) {
    if (type == AnnotType::Popup || type == AnnotType::Widget || type == AnnotType::Unknown) {
        return {};
    }
    if (type == AnnotType::ThreeD) {
        return "threed";
    }
    std::string name(annotTypeName(type));
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
    return name;
}

AnnotType xfdfType(std::string_view element) {
    for (int i = 0; i < kAnnotTypeCount; ++i) {
        const auto t = static_cast<AnnotType>(i);
        const std::string name = xfdfElement(t);
        if (!name.empty() && name == element) {
            return t;
        }
    }
    return AnnotType::Unknown;
}

std::vector<float> parseNumbers(std::string_view text) {
    std::vector<float> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ',' || text[i] == ';' || text[i] == ' ' || text[i] == '\t' ||
                                   text[i] == '\r' || text[i] == '\n')) {
            ++i;
        }
        if (i >= text.size()) {
            break;
        }
        float v = 0;
        const auto r = std::from_chars(text.data() + i, text.data() + text.size(), v);
        if (r.ec != std::errc{}) {
            ++i;  // мусор — пропустить символ
            continue;
        }
        out.push_back(v);
        i = static_cast<std::size_t>(r.ptr - text.data());
    }
    return out;
}

std::string formatNumbers(const std::vector<float>& values, char separator) {
    std::string out;
    char buf[32];
    for (const float v : values) {
        if (!out.empty()) {
            out.push_back(separator);
        }
        // Четыре знака после запятой, кратчайшая запись (to_chars).
        const float rounded = std::round(v * 10000.0f) / 10000.0f;
        const auto r = std::to_chars(buf, buf + sizeof(buf), rounded == 0.0f ? 0.0f : rounded);
        out.append(buf, r.ptr);
    }
    return out;
}

// ---------------------------------------------------------------- экспорт

XfdfDocument exportXfdf(Document& doc, const std::vector<AnnotRef>& refs, const std::string& fileName) {
    detail::pdfOf(doc);
    XfdfDocument out;
    out.file = fileName;
    const std::vector<AnnotRef> set = detail::exchangeSet(doc, refs);
    std::map<int, std::unique_ptr<detail::PageAnnots>> pages;
    for (const AnnotRef& ref : set) {
        auto& pa = pages[ref.page];
        if (!pa) {
            pa = std::make_unique<detail::PageAnnots>(doc, ref.page);
        }
        pdf_obj* obj = pa->object(ref.objnum);
        if (obj == nullptr) {
            continue;
        }
        XRaw raw;
        fz_context* ctx = pa->ctx();
        mu::call(ctx, [&] { readRaw(ctx, obj, &raw); });
        XfdfAnnot a = toXfdf(ref.page, raw);
        if (!a.element.empty()) {
            out.annots.push_back(std::move(a));
        }
    }
    return out;
}

ExchangeInfo probeXfdf(const XfdfDocument& xfdf) {
    ExchangeInfo info;
    info.file = xfdf.file;
    for (const XfdfAnnot& a : xfdf.annots) {
        if (xfdfType(a.element) == AnnotType::Unknown) {
            continue;
        }
        ++info.annots;
        info.maxPage = std::max(info.maxPage, static_cast<int>(numberOr(a.attribute("page"), 0)));
    }
    return info;
}

// ---------------------------------------------------------------- импорт

ImportReport importXfdf(Document& doc, const XfdfDocument& xfdf, const std::string& documentFile) {
    pdf_document* pdf = detail::pdfOf(doc);
    if (!sameDocumentName(xfdf.file, documentFile)) {
        throw ExchangeError(ExchangeError::Kind::ForeignDocument,
                            "XFDF belongs to another document: " + xfdf.file, xfdf.file);
    }
    fz_context* ctx = doc.context().get();
    ImportReport report;
    std::set<int> skippedPages;
    std::map<int, std::vector<int>> touched;
    std::map<int, std::unique_ptr<detail::PageAnnots>> pages;
    const auto pageAnnots = [&](int page) -> detail::PageAnnots& {
        auto& pa = pages[page];
        if (!pa) {
            pa = std::make_unique<detail::PageAnnots>(doc, page);
        }
        return *pa;
    };
    struct Placed {
        std::size_t index;
        int page;
        int num;
    };
    std::vector<Placed> placed;
    std::unordered_map<std::string, int> byName;  // /NM → номер перенесённой
    Journal::Operation op(doc, "Import comments");
    for (std::size_t i = 0; i < xfdf.annots.size(); ++i) {
        const XfdfAnnot& a = xfdf.annots[i];
        const AnnotType type = xfdfType(a.element);
        if (type == AnnotType::Unknown) {
            continue;
        }
        XNew n = fromXfdf(a, type);
        if (n.page < 0 || n.page >= doc.pageCount()) {
            ++report.skipped;
            skippedPages.insert(n.page);
            continue;
        }
        if (n.nm.empty()) {
            n.nm = newAnnotName();
        }
        mu::BufferHandle file(ctx);
        mu::PdfObjHandle ref(ctx);
        const std::string& data = a.fileData;
        const bool withFile = type == AnnotType::FileAttachment && a.hasFileData;
        mu::call(ctx, [&] {
            if (withFile) {
                file.reset(fz_new_buffer_from_copied_data(
                    ctx, reinterpret_cast<const unsigned char*>(data.data()), data.size()));
            }
            ref.reset(buildDict(ctx, pdf, n, withFile ? file.get() : nullptr));
        });
        detail::PageAnnots& pa = pageAnnots(n.page);
        const bool replaced = detail::placeImported(pa, ref, n.nm);
        pa.refresh();
        int num = 0;
        mu::call(ctx, [&] { num = pdf_to_num(ctx, ref); });
        placed.push_back({i, n.page, num});
        byName[n.nm] = num;
        touched[n.page].push_back(num);
        (replaced ? report.updated : report.created) += 1;
        report.refs.push_back({n.page, num, n.nm});
    }
    // Ответы и группы: /IRT по /NM родителя — перенесённого или уже бывшего на странице.
    for (const Placed& p : placed) {
        const std::string parent = xfdf.annots[p.index].attribute("inreplyto");
        if (parent.empty()) {
            continue;
        }
        int target = 0;
        if (const auto it = byName.find(parent); it != byName.end()) {
            target = it->second;
        } else {
            target = detail::findByName(pageAnnots(p.page), parent);
        }
        if (target > 0 && target != p.num) {
            const int self = p.num;
            mu::call(ctx, [&] {
                pdf_obj* obj = pdf_load_object(ctx, pdf, self);
                pdf_dict_put_drop(ctx, obj, PDF_NAME(IRT), pdf_new_indirect(ctx, pdf, target, 0));
                pdf_drop_obj(ctx, obj);
            });
        }
    }
    // Окна записок.
    for (const Placed& p : placed) {
        const XfdfAnnot& a = xfdf.annots[p.index];
        if (!a.hasPopup) {
            continue;
        }
        mu::PdfObjHandle popup(ctx);
        mu::PdfObjHandle parent(ctx);
        const RectF r = a.popupRect;
        const int flags = a.popupFlags;
        const bool open = a.popupOpen;
        const int num = p.num;
        mu::call(ctx, [&] {
            popup.reset(pdf_add_new_dict(ctx, pdf, 6));
            pdf_dict_put_rect(ctx, popup, PDF_NAME(Rect), fz_make_rect(r.x0, r.y0, r.x1, r.y1));
            pdf_dict_put_bool(ctx, popup, PDF_NAME(Open), open ? 1 : 0);
            if (flags != 0) {
                pdf_dict_put_int(ctx, popup, PDF_NAME(F), flags);
            }
            parent.reset(pdf_new_indirect(ctx, pdf, num, 0));
        });
        detail::attachPopup(pageAnnots(p.page), parent, popup);
    }
    pages.clear();
    detail::finishImported(doc, touched);
    for (const auto& [page, list] : touched) {
        op.invalidate(page);
    }
    op.commit();
    report.skippedPages.assign(skippedPages.begin(), skippedPages.end());
    return report;
}

}  // namespace ot::annot
