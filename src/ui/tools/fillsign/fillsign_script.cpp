// Действия --script Fill & Sign (план 5.3, п. 4; рецепт «Новое действие --script»; страницы — с 1, точки и
// рамки — пункты от левого верхнего угла страницы):
//   fillsign.placeText{page, point, text, size, color} — текст щелчком в точке, как инструмент: умное
//     размещение по геометрии бланка (на линии — базовой линией на линию, в клетке — от её края, гребёнка —
//     по символу в клетку одним шагом), без привязки — кеглем size (по умолчанию 10); цвет «#rrggbb»;
//   fillsign.placeMark{page, point, mark, color} — отметка (check | cross | dot | line | box: ✓ ✕ ● — ▭) во
//   флажок
//     под точкой или центром в точке;
//   fillsign.placeSignature{page, rect, kind, strokes, image, save} — подпись или инициалы (kind: signature |
//     initials) штампом-картинкой в рамке rect (с пропорциями картинки): strokes — «Нарисовать» (штрихи
//     [[[x, y], …], …] в px поля), image — «Картинка» (PNG/JPEG; белый фон убирается), без них — сохранённая;
//     save — сохранить картинку в <данные>/signatures (sign-02);
//   fillsign.flatCopy{path} — «Сохранить как → Плоская копия» (аннотации — в содержимое страниц копии).
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonObject>

#include "core/annot/fillsign.h"
#include "core/doc/saver.h"
#include "services/annot_ops.h"
#include "services/comment_settings.h"
#include "services/document_session.h"
#include "services/fillsign_ops.h"
#include "services/saved_signatures.h"
#include "services/script_runner.h"
#include "services/settings.h"
#include "services/signature_image.h"
#include "ui/app/script_actions.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/tools/fillsign/fillsign_script.h"

namespace ot::ui {

namespace {

int fillSignPage(DocumentView& doc, const QJsonObject& a) {
    const int page = a.value(QLatin1String("page")).toInt() - 1;
    if (page >= doc.session().pageCount()) {
        throw ScriptError(QStringLiteral("page %1 is out of range").arg(page + 1));
    }
    return page;
}

annot::AnnotColor fillSignColor(const QJsonObject& a) {
    if (!a.contains(QLatin1String("color"))) {
        return annot::AnnotColor::gray(0);
    }
    const auto color = comments::parseColor(a.value(QLatin1String("color")).toString());
    if (!color) {
        throw ScriptError(QStringLiteral("bad color: %1").arg(a.value(QLatin1String("color")).toString()));
    }
    return *color;
}

// Картинка подписи: штрихи, файл или сохранённая.
QImage fillSignImage(const QJsonObject& a, ScriptContext& c, fillsign::SignatureKind kind) {
    if (a.contains(QLatin1String("strokes"))) {
        std::vector<std::vector<QPointF>> strokes;
        double w = 1;
        double h = 1;
        for (const QJsonValue& s : a.value(QLatin1String("strokes")).toArray()) {
            std::vector<QPointF> stroke;
            for (const QJsonValue& p : s.toArray()) {
                const QJsonArray xy = p.toArray();
                stroke.emplace_back(xy.at(0).toDouble(), xy.at(1).toDouble());
                w = std::max(w, xy.at(0).toDouble() + 4);
                h = std::max(h, xy.at(1).toDouble() + 4);
            }
            strokes.push_back(std::move(stroke));
        }
        return fillsign::strokesToImage(strokes, QSizeF(w, h), QColor(Qt::black), 2.5, 3.0);
    }
    if (a.contains(QLatin1String("image"))) {
        const QString path = c.path(a.value(QLatin1String("image")).toString());
        QImageReader reader(path);
        const QImage source = reader.read();
        if (source.isNull()) {
            throw ScriptError(QStringLiteral("cannot read image %1").arg(QFileInfo(path).fileName()));
        }
        return fillsign::cropToInk(
            fillsign::removeBackground(source, fillsign::kDefaultThreshold, true, QColor(Qt::black)));
    }
    const QImage saved = fillsign::SavedSignatures().load(kind);
    if (saved.isNull()) {
        throw ScriptError(QStringLiteral("no saved %1: give strokes or image").arg(fillsign::kindName(kind)));
    }
    return saved;
}

}  // namespace

void registerFillSignScriptActions(ScriptRunner& runner, MainWindow& w) {
    MainWindow* window = &w;
    runner.registerAction(
        QStringLiteral("fillsign.placeText"),
        R"({"type": "object", "properties": {
            "page": {"type": "integer", "minimum": 1},
            "point": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2},
            "text": {"type": "string", "minLength": 1},
            "size": {"type": "number", "minimum": 4, "maximum": 72},
            "color": {"type": "string", "minLength": 4}},
            "required": ["page", "point", "text"], "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext&) {
            DocumentView& doc = script::document(*window);
            const int page = fillSignPage(doc, a);
            const QJsonArray xy = a.value(QLatin1String("point")).toArray();
            const PointF point{static_cast<float>(xy.at(0).toDouble()),
                               static_cast<float>(xy.at(1).toDouble())};
            const auto size =
                static_cast<float>(a.value(QLatin1String("size")).toDouble(fillsign::kDefaultFontSize));
            const annot::AnnotColor color = fillSignColor(a);
            const QString text = a.value(QLatin1String("text")).toString();
            const fillsign::PageGeometry g =
                script::await(fillsign::geometry(doc.session(), page), "fillsign.placeText");
            const fillsign::Placement placement = fillsign::placeText(g, point, size);
            std::vector<annot::AnnotProps> items;
            if (placement.kind == fillsign::FieldKind::Comb) {
                items = fillsign::combProps(placement, text.toStdU32String(), color);
            } else {
                const auto width = static_cast<float>(doc.session().pageSizePoints(page).width());
                items.push_back(fillsign::textProps(placement, text.toStdString(), color, width));
            }
            const std::string author = comments::author(Settings());
            for (annot::AnnotProps& p : items) {
                p.author = author;
            }
            script::await(fillsign::createAll(doc.session(), page, std::move(items)), "fillsign.placeText");
        });
    runner.registerAction(
        QStringLiteral("fillsign.placeMark"),
        R"({"type": "object", "properties": {
            "page": {"type": "integer", "minimum": 1},
            "point": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2},
            "mark": {"enum": ["check", "cross", "dot", "line", "box"]},
            "size": {"type": "number", "minimum": 4, "maximum": 72},
            "color": {"type": "string", "minLength": 4}},
            "required": ["page", "point", "mark"], "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext&) {
            DocumentView& doc = script::document(*window);
            const int page = fillSignPage(doc, a);
            const QJsonArray xy = a.value(QLatin1String("point")).toArray();
            const PointF point{static_cast<float>(xy.at(0).toDouble()),
                               static_cast<float>(xy.at(1).toDouble())};
            const QString name = a.value(QLatin1String("mark")).toString();
            const fillsign::Mark mark = name == QLatin1String("cross")  ? fillsign::Mark::Cross
                                        : name == QLatin1String("dot")  ? fillsign::Mark::Dot
                                        : name == QLatin1String("line") ? fillsign::Mark::Line
                                        : name == QLatin1String("box")  ? fillsign::Mark::Box
                                                                        : fillsign::Mark::Check;
            const auto size =
                static_cast<float>(a.value(QLatin1String("size")).toDouble(fillsign::kMarkSize));
            const fillsign::PageGeometry g =
                script::await(fillsign::geometry(doc.session(), page), "fillsign.placeMark");
            annot::AnnotProps props =
                fillsign::markProps(mark, fillsign::placeMark(g, point, size), fillSignColor(a));
            props.author = comments::author(Settings());
            script::await(annot::create(doc.session(), page, std::move(props),
                                        QString::fromLatin1(fillsign::kAddMarkStep)),
                          "fillsign.placeMark");
        });
    runner.registerAction(
        QStringLiteral("fillsign.placeSignature"),
        R"({"type": "object", "properties": {
            "page": {"type": "integer", "minimum": 1},
            "rect": {"type": "array", "items": {"type": "number"}, "minItems": 4, "maxItems": 4},
            "kind": {"enum": ["signature", "initials"]},
            "strokes": {"type": "array", "minItems": 1, "items": {"type": "array", "minItems": 1,
                "items": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2}}},
            "image": {"type": "string", "minLength": 1},
            "save": {"type": "boolean"}},
            "required": ["page", "rect"], "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext& c) {
            DocumentView& doc = script::document(*window);
            const int page = fillSignPage(doc, a);
            const bool initials = a.value(QLatin1String("kind")).toString() == QLatin1String("initials");
            const fillsign::SignatureKind kind =
                initials ? fillsign::SignatureKind::Initials : fillsign::SignatureKind::Signature;
            const QImage image = fillSignImage(a, c, kind);
            if (!fillsign::hasInk(image)) {
                throw ScriptError(QStringLiteral("signature image is empty"));
            }
            if (a.value(QLatin1String("save")).toBool()) {
                QString error;
                if (!fillsign::SavedSignatures().save(kind, image, &error)) {
                    throw ScriptError(error);
                }
            }
            const QJsonArray r = a.value(QLatin1String("rect")).toArray();
            const QRectF rect(QPointF(r.at(0).toDouble(), r.at(1).toDouble()),
                              QPointF(r.at(2).toDouble(), r.at(3).toDouble()));
            if (rect.normalized().isEmpty()) {
                throw ScriptError(QStringLiteral("rect is empty"));
            }
            // Рамка — с пропорциями картинки, от левого верхнего угла rect.
            const QSizeF size = QSizeF(image.size()).scaled(rect.normalized().size(), Qt::KeepAspectRatio);
            const QRectF target(rect.normalized().topLeft(), size);
            annot::AnnotProps props;
            props.type = annot::AnnotType::Stamp;
            props.rect = {static_cast<float>(target.left()), static_cast<float>(target.top()),
                          static_cast<float>(target.right()), static_cast<float>(target.bottom())};
            props.author = comments::author(Settings());
            props.subject = initials ? "Initials" : "Signature";
            const QByteArray png = fillsign::toPng(image);
            script::await(
                fillsign::createSignature(doc.session(), page,
                                          std::string(png.constData(), static_cast<std::size_t>(png.size())),
                                          std::move(props), initials),
                "fillsign.placeSignature");
        });
    runner.registerAction(
        QStringLiteral("fillsign.flatCopy"),
        R"({"type": "object", "properties": {"path": {"type": "string", "minLength": 1}},
            "required": ["path"], "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext& c) {
            DocumentView& doc = script::document(*window);
            SaveOptions options;
            options.flattenAnnotations = true;
            options.allowBreakingSignatures = true;  // копия; документ остаётся как есть
            const SaveReport report = script::await(
                doc.session().saveCopy(c.path(a.value(QLatin1String("path")).toString()), options),
                "fillsign.flatCopy");
            if (!report.ok()) {
                throw ScriptError(
                    QStringLiteral("flat copy failed: %1").arg(QString::fromStdString(report.message)));
            }
        });
}

}  // namespace ot::ui
