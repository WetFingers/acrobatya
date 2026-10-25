// JSON свойств аннотации (буфер обмена, XFDF 5.4, --script), /NM и цвета (план 5.1): ключи — имена ключей PDF
// без «/» там, где они есть (C, IC, CA, BS…), геометрия — координаты страницы.
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <stdexcept>

#include "core/annot/annots.h"

namespace ot::annot {

namespace annot_json_detail {

using nlohmann::json;

json colorJson(const AnnotColor& c) {
    json arr = json::array();
    for (int i = 0; i < c.n; ++i) {
        arr.push_back(c.c[static_cast<std::size_t>(i)]);
    }
    return arr;
}

AnnotColor colorFrom(const json& j) {
    AnnotColor c;
    if (!j.is_array()) {
        return c;
    }
    const int n = static_cast<int>(j.size());
    if (n != 1 && n != 3 && n != 4) {
        return c;
    }
    c.n = n;
    for (int i = 0; i < n; ++i) {
        c.c[static_cast<std::size_t>(i)] = j[static_cast<std::size_t>(i)].get<float>();
    }
    return c;
}

json pointJson(PointF p) { return json::array({p.x, p.y}); }
PointF pointFrom(const json& j) { return {j.at(0).get<float>(), j.at(1).get<float>()}; }
json rectJson(const RectF& r) { return json::array({r.x0, r.y0, r.x1, r.y1}); }
RectF rectFrom(const json& j) {
    return {j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>(), j.at(3).get<float>()};
}

json pointsJson(const std::vector<PointF>& points) {
    json arr = json::array();
    for (const PointF p : points) {
        arr.push_back(pointJson(p));
    }
    return arr;
}

std::vector<PointF> pointsFrom(const json& j) {
    std::vector<PointF> out;
    for (const json& p : j) {
        out.push_back(pointFrom(p));
    }
    return out;
}

}  // namespace annot_json_detail

AnnotColor AnnotColor::fromRgb(std::uint32_t rgb) noexcept {
    return AnnotColor::rgb(static_cast<float>((rgb >> 16) & 0xff) / 255.0f,
                           static_cast<float>((rgb >> 8) & 0xff) / 255.0f,
                           static_cast<float>(rgb & 0xff) / 255.0f);
}

std::uint32_t AnnotColor::toRgb() const noexcept {
    float r = 0;
    float g = 0;
    float b = 0;
    switch (n) {
        case 1:
            r = g = b = c[0];
            break;
        case 3:
            r = c[0];
            g = c[1];
            b = c[2];
            break;
        case 4:
            r = (1 - c[0]) * (1 - c[3]);
            g = (1 - c[1]) * (1 - c[3]);
            b = (1 - c[2]) * (1 - c[3]);
            break;
        default:
            return 0;
    }
    auto byte = [](float v) {
        return static_cast<std::uint32_t>(std::lround(std::fmin(1.0f, std::fmax(0.0f, v)) * 255.0f));
    };
    return (byte(r) << 16) | (byte(g) << 8) | byte(b);
}

bool AnnotPatch::empty() const noexcept {
    return !contents && !richContents && !author && !subject && !icon && !color && !interiorColor &&
           !opacity && !borderWidth && !borderStyle && !dash && !cloudy && !flags && !open && !popupRect &&
           !quads && !ink && !vertices && !fontSize && !textColor && !align && !fontName && !callout &&
           !lineStartStyle && !lineEndStyle && !lineStart && !lineEnd && !cloudIntensity && !rect;
}

std::string newAnnotName() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> dist;
    std::uint64_t hi = dist(rng);
    std::uint64_t lo = dist(rng);
    hi = (hi & 0xffffffffffff0fffULL) | 0x0000000000004000ULL;  // версия 4
    lo = (lo & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;  // вариант RFC 4122
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx", static_cast<unsigned>(hi >> 32),
                  static_cast<unsigned>((hi >> 16) & 0xffff), static_cast<unsigned>(hi & 0xffff),
                  static_cast<unsigned>(lo >> 48), static_cast<unsigned long long>(lo & 0xffffffffffffULL));
    return buf;
}

std::string toJson(const AnnotProps& p) {
    using namespace annot_json_detail;
    json j;
    j["subtype"] = std::string(annotTypeName(p.type));
    j["rect"] = rectJson(p.rect);
    j["F"] = p.flags;
    j["NM"] = p.nm;
    j["Contents"] = p.contents;
    j["RC"] = p.richContents;
    j["T"] = p.author;
    j["Subj"] = p.subject;
    j["Name"] = p.icon;
    j["C"] = colorJson(p.color);
    j["IC"] = colorJson(p.interiorColor);
    j["CA"] = p.opacity;
    j["BS"] = {{"W", p.borderWidth}, {"S", static_cast<int>(p.borderStyle)}, {"D", p.dash}};
    j["BE"] = {{"cloudy", p.cloudy}, {"I", p.cloudIntensity}};
    json quads = json::array();
    for (const QuadF& q : p.quads) {
        quads.push_back(json::array({q.ul.x, q.ul.y, q.ur.x, q.ur.y, q.ll.x, q.ll.y, q.lr.x, q.lr.y}));
    }
    j["QuadPoints"] = quads;
    json ink = json::array();
    for (const Stroke& s : p.ink) {
        ink.push_back(pointsJson(s));
    }
    j["InkList"] = ink;
    j["Vertices"] = pointsJson(p.vertices);
    j["L"] = json::array({pointJson(p.lineStart), pointJson(p.lineEnd)});
    j["LE"] = json::array({static_cast<int>(p.lineStartStyle), static_cast<int>(p.lineEndStyle)});
    j["IT"] = p.intent;
    j["DA"] = {{"font", p.fontName}, {"size", p.fontSize}, {"color", colorJson(p.textColor)}};
    j["Q"] = p.align;
    j["CL"] = pointsJson(p.callout);
    j["Popup"] = p.hasPopup() ? rectJson(p.popupRect) : json();
    j["Open"] = p.open;
    j["IRT"] = p.inReplyTo;
    j["RT"] = p.replyType;
    j["CreationDate"] = p.created;
    j["M"] = p.modified;
    return j.dump();
}

AnnotProps propsFromJson(std::string_view text) {
    using namespace annot_json_detail;
    AnnotProps p;
    try {
        const json j = json::parse(text);
        if (!j.is_object()) {
            throw std::invalid_argument("annotation JSON is not an object");
        }
        p.type = annotTypeFromName(j.value("subtype", std::string()));
        if (j.contains("rect")) {
            p.rect = rectFrom(j["rect"]);
        }
        p.flags = j.value("F", kAnnotPrint);
        p.nm = j.value("NM", std::string());
        p.contents = j.value("Contents", std::string());
        p.richContents = j.value("RC", std::string());
        p.author = j.value("T", std::string());
        p.subject = j.value("Subj", std::string());
        p.icon = j.value("Name", std::string());
        p.color = colorFrom(j.value("C", json::array()));
        p.interiorColor = colorFrom(j.value("IC", json::array()));
        p.opacity = j.value("CA", 1.0f);
        if (const auto bs = j.find("BS"); bs != j.end() && bs->is_object()) {
            p.borderWidth = bs->value("W", 1.0f);
            p.borderStyle = static_cast<BorderStyle>(std::clamp(bs->value("S", 0), 0, 4));
            p.dash = bs->value("D", std::vector<float>{});
        }
        if (const auto be = j.find("BE"); be != j.end() && be->is_object()) {
            p.cloudy = be->value("cloudy", false);
            p.cloudIntensity = be->value("I", 1.0f);
        }
        for (const json& q : j.value("QuadPoints", json::array())) {
            const std::vector<float> v = q.get<std::vector<float>>();
            if (v.size() == 8) {
                p.quads.push_back({{v[0], v[1]}, {v[2], v[3]}, {v[4], v[5]}, {v[6], v[7]}});
            }
        }
        for (const json& s : j.value("InkList", json::array())) {
            p.ink.push_back(pointsFrom(s));
        }
        p.vertices = pointsFrom(j.value("Vertices", json::array()));
        if (const auto l = j.find("L"); l != j.end() && l->is_array() && l->size() == 2) {
            p.lineStart = pointFrom((*l)[0]);
            p.lineEnd = pointFrom((*l)[1]);
        }
        if (const auto le = j.find("LE"); le != j.end() && le->is_array() && le->size() == 2) {
            p.lineStartStyle = static_cast<LineEnding>(std::clamp((*le)[0].get<int>(), 0, 9));
            p.lineEndStyle = static_cast<LineEnding>(std::clamp((*le)[1].get<int>(), 0, 9));
        }
        p.intent = j.value("IT", std::string());
        if (const auto da = j.find("DA"); da != j.end() && da->is_object()) {
            p.fontName = da->value("font", std::string("Helv"));
            p.fontSize = da->value("size", 12.0f);
            p.textColor = colorFrom(da->value("color", json::array()));
        }
        p.align = j.value("Q", 0);
        p.callout = pointsFrom(j.value("CL", json::array()));
        if (const auto popup = j.find("Popup"); popup != j.end() && popup->is_array()) {
            p.popupRect = rectFrom(*popup);
        }
        p.open = j.value("Open", false);
        p.inReplyTo = j.value("IRT", 0);
        p.replyType = j.value("RT", std::string());
        p.created = j.value("CreationDate", std::int64_t{0});
        p.modified = j.value("M", std::int64_t{0});
    } catch (const json::exception& e) {
        throw std::invalid_argument(std::string("annotation JSON: ") + e.what());
    }
    return p;
}

}  // namespace ot::annot
