// Действия --script комментариев (план 5.1; рецепт «Новое действие --script»):
//   annot.create{type, page, rect, points, contents, color, author, icon, opacity, width, nm} — создать
//   комментарий
//     одного из 15 создаваемых типов на странице page (с 1): rect — [x0, y0, x1, y1] в пунктах от левого
//     верхнего угла страницы; points — вершины [[x, y], …] для Line, Polygon, PolyLine, Ink (без них —
//     из rect: диагональ, треугольник, ломаная, волна); разметка — один четырёхугольник по rect; цвет —
//     «#rrggbb»; автор — из Identity Установок, если не задан. Шаг истории — createStepName типа.
#include <QJsonArray>
#include <QJsonObject>

#include "core/annot/annots.h"
#include "services/annot_ops.h"
#include "services/comment_settings.h"
#include "services/document_session.h"
#include "services/script_runner.h"
#include "services/settings.h"
#include "ui/app/script_actions.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/tools/comment/comment_script.h"

namespace ot::ui {

namespace {

using annot::AnnotProps;
using annot::AnnotType;

PointF scriptPoint(const QJsonValue& v) {
    const QJsonArray a = v.toArray();
    return {static_cast<float>(a.at(0).toDouble()), static_cast<float>(a.at(1).toDouble())};
}

// Геометрия типа из rect и points.
void scriptGeometry(AnnotProps& p, const std::optional<RectF>& rect, const std::vector<PointF>& points) {
    if (rect) {
        p.rect = *rect;
    }
    const RectF r = rect.value_or(RectF{});
    const float mx = (r.x0 + r.x1) / 2;
    switch (p.type) {
        case AnnotType::Highlight:
        case AnnotType::Underline:
        case AnnotType::Squiggly:
        case AnnotType::StrikeOut:
            if (rect) {
                p.quads = {{{r.x0, r.y0}, {r.x1, r.y0}, {r.x0, r.y1}, {r.x1, r.y1}}};
            }
            break;
        case AnnotType::Line:
            if (points.size() >= 2) {
                p.lineStart = points.front();
                p.lineEnd = points.back();
            } else if (rect) {
                p.lineStart = {r.x0, r.y1};
                p.lineEnd = {r.x1, r.y0};
            }
            break;
        case AnnotType::Polygon:
            p.vertices = !points.empty() || !rect
                             ? points
                             : std::vector<PointF>{{r.x0, r.y1}, {mx, r.y0}, {r.x1, r.y1}};
            break;
        case AnnotType::PolyLine:
            p.vertices =
                !points.empty() || !rect
                    ? points
                    : std::vector<PointF>{{r.x0, r.y1}, {(r.x0 + mx) / 2, r.y0}, {mx, r.y1}, {r.x1, r.y0}};
            break;
        case AnnotType::Ink:
            if (!points.empty()) {
                p.ink = {points};
            } else if (rect) {
                annot::Stroke wave;
                for (int i = 0; i <= 8; ++i) {
                    wave.push_back(
                        {r.x0 + r.width() * static_cast<float>(i) / 8.0f, i % 2 == 0 ? r.y1 : r.y0});
                }
                p.ink = {wave};
            }
            break;
        default:
            break;
    }
}

}  // namespace

void registerCommentScriptActions(ScriptRunner& runner, MainWindow& w) {
    MainWindow* window = &w;
    runner.registerAction(
        QStringLiteral("annot.create"),
        R"({"type": "object", "properties": {
            "type": {"type": "string", "enum": ["Text", "FreeText", "Line", "Square", "Circle", "Polygon",
                "PolyLine", "Highlight", "Underline", "Squiggly", "StrikeOut", "Stamp", "Caret", "Ink",
                "FileAttachment"]},
            "page": {"type": "integer", "minimum": 1},
            "rect": {"type": "array", "items": {"type": "number"}, "minItems": 4, "maxItems": 4},
            "points": {"type": "array", "minItems": 2,
                "items": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2}},
            "contents": {"type": "string"}, "color": {"type": "string", "minLength": 4},
            "author": {"type": "string"}, "icon": {"type": "string", "minLength": 1},
            "opacity": {"type": "number", "minimum": 0.05, "maximum": 1},
            "width": {"type": "number", "minimum": 0, "maximum": 12},
            "nm": {"type": "string", "minLength": 1}},
            "required": ["type", "page"], "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext&) {
            DocumentView& doc = script::document(*window);
            const int page = a.value(QLatin1String("page")).toInt() - 1;
            if (page >= doc.session().pageCount()) {
                throw ScriptError(QStringLiteral("page %1 is out of range").arg(page + 1));
            }
            const AnnotType type =
                annot::annotTypeFromName(a.value(QLatin1String("type")).toString().toStdString());
            const Settings settings;
            AnnotProps props = comments::newProps(settings, type);
            std::optional<RectF> rect;
            if (a.contains(QLatin1String("rect"))) {
                const QJsonArray r = a.value(QLatin1String("rect")).toArray();
                rect = RectF{static_cast<float>(r.at(0).toDouble()), static_cast<float>(r.at(1).toDouble()),
                             static_cast<float>(r.at(2).toDouble()), static_cast<float>(r.at(3).toDouble())};
                if (rect->empty()) {
                    throw ScriptError(QStringLiteral("rect is empty"));
                }
            }
            std::vector<PointF> points;
            for (const QJsonValue& v : a.value(QLatin1String("points")).toArray()) {
                points.push_back(scriptPoint(v));
            }
            if (!rect && points.empty()) {
                throw ScriptError(QStringLiteral("rect or points are required"));
            }
            scriptGeometry(props, rect, points);
            props.contents = a.value(QLatin1String("contents")).toString().toStdString();
            if (a.contains(QLatin1String("color"))) {
                const auto color = comments::parseColor(a.value(QLatin1String("color")).toString());
                if (!color) {
                    throw ScriptError(
                        QStringLiteral("bad color: %1").arg(a.value(QLatin1String("color")).toString()));
                }
                props.color = *color;
            }
            if (a.contains(QLatin1String("author"))) {
                props.author = a.value(QLatin1String("author")).toString().toStdString();
            }
            if (a.contains(QLatin1String("nm"))) {
                props.nm = a.value(QLatin1String("nm")).toString().toStdString();  // 5.4: ответы по /NM
            }
            if (a.contains(QLatin1String("icon"))) {
                props.icon = a.value(QLatin1String("icon")).toString().toStdString();
            }
            if (a.contains(QLatin1String("opacity"))) {
                props.opacity = static_cast<float>(a.value(QLatin1String("opacity")).toDouble());
            }
            if (a.contains(QLatin1String("width"))) {
                props.borderWidth = static_cast<float>(a.value(QLatin1String("width")).toDouble());
            }
            if (type == AnnotType::FileAttachment) {
                props.fileName = "note.txt";
                props.fileData = props.contents.empty() ? std::string("attachment") : props.contents;
            }
            script::await(annot::create(doc.session(), page, std::move(props)), "annot.create");
        });
}

}  // namespace ot::ui
