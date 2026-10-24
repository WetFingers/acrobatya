#include "services/comment_settings.h"

#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

#include "services/settings_keys.h"

namespace ot::comments {

namespace {

QJsonObject stylesObject(const Settings& settings) {
    const QString raw = settings.get(settings::kCommentToolStyles);
    if (raw.isEmpty()) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    return doc.isObject() ? doc.object() : QJsonObject();
}

}  // namespace

annot::Identity identity(const Settings& settings) {
    annot::Identity id;
    id.name = settings.get(settings::kCommentIdentityName).toStdString();
    id.title = settings.get(settings::kCommentIdentityTitle).toStdString();
    id.organization = settings.get(settings::kCommentIdentityOrganization).toStdString();
    id.alwaysLoginName = settings.get(settings::kCommentAlwaysLoginName);
    return id;
}

std::string author(const Settings& settings) { return annot::authorName(identity(settings)); }

QString colorName(const annot::AnnotColor& color) {
    if (color.empty()) {
        return {};
    }
    return QColor::fromRgb(static_cast<QRgb>(color.toRgb())).name(QColor::HexRgb);
}

std::optional<annot::AnnotColor> parseColor(const QString& name) {
    const QColor c = QColor::fromString(name);
    if (!c.isValid()) {
        return std::nullopt;
    }
    return annot::AnnotColor::fromRgb(static_cast<std::uint32_t>(c.rgb() & 0xffffff));
}

ToolStyle toolStyle(const Settings& settings, const QString& tool) {
    ToolStyle style;
    const QJsonObject o = stylesObject(settings).value(tool).toObject();
    if (o.contains(QLatin1String("color"))) {
        style.color = parseColor(o.value(QLatin1String("color")).toString());
    }
    if (o.contains(QLatin1String("opacity"))) {
        style.opacity =
            static_cast<float>(std::clamp(o.value(QLatin1String("opacity")).toDouble(1.0), 0.05, 1.0));
    }
    if (o.contains(QLatin1String("width"))) {
        style.width =
            static_cast<float>(std::clamp(o.value(QLatin1String("width")).toDouble(1.0), 0.0, 12.0));
    }
    if (o.contains(QLatin1String("icon"))) {
        style.icon = o.value(QLatin1String("icon")).toString().toStdString();
    }
    if (o.contains(QLatin1String("font"))) {
        style.font = o.value(QLatin1String("font")).toString().toStdString();
    }
    if (o.contains(QLatin1String("fontSize"))) {
        style.fontSize =
            static_cast<float>(std::clamp(o.value(QLatin1String("fontSize")).toDouble(12.0), 4.0, 144.0));
    }
    if (o.contains(QLatin1String("textColor"))) {
        style.textColor = parseColor(o.value(QLatin1String("textColor")).toString());
    }
    if (o.contains(QLatin1String("align"))) {
        style.align = std::clamp(o.value(QLatin1String("align")).toInt(0), 0, 2);
    }
    if (o.contains(QLatin1String("fill"))) {
        style.fill = parseColor(o.value(QLatin1String("fill")).toString());
    }
    if (o.contains(QLatin1String("lineStart"))) {
        style.lineStart = std::clamp(o.value(QLatin1String("lineStart")).toInt(0), 0, 9);
    }
    if (o.contains(QLatin1String("lineEnd"))) {
        style.lineEnd = std::clamp(o.value(QLatin1String("lineEnd")).toInt(0), 0, 9);
    }
    return style;
}

void setToolStyle(Settings& settings, const QString& tool, const ToolStyle& style) {
    QJsonObject all = stylesObject(settings);
    QJsonObject o;
    if (style.color) {
        o.insert(QLatin1String("color"), colorName(*style.color));
    }
    if (style.opacity) {
        o.insert(QLatin1String("opacity"), static_cast<double>(*style.opacity));
    }
    if (style.width) {
        o.insert(QLatin1String("width"), static_cast<double>(*style.width));
    }
    if (style.icon) {
        o.insert(QLatin1String("icon"), QString::fromStdString(*style.icon));
    }
    if (style.font) {
        o.insert(QLatin1String("font"), QString::fromStdString(*style.font));
    }
    if (style.fontSize) {
        o.insert(QLatin1String("fontSize"), static_cast<double>(*style.fontSize));
    }
    if (style.textColor) {
        o.insert(QLatin1String("textColor"), colorName(*style.textColor));
    }
    if (style.align) {
        o.insert(QLatin1String("align"), *style.align);
    }
    if (style.fill) {
        o.insert(QLatin1String("fill"), colorName(*style.fill));
    }
    if (style.lineStart) {
        o.insert(QLatin1String("lineStart"), *style.lineStart);
    }
    if (style.lineEnd) {
        o.insert(QLatin1String("lineEnd"), *style.lineEnd);
    }
    all.insert(tool, o);
    settings.set(settings::kCommentToolStyles,
                 QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact)));
}

annot::AnnotProps newProps(const Settings& settings, annot::AnnotType type, const QString& tool) {
    annot::AnnotProps p = annot::toolDefaults(type);
    const QString key = tool.isEmpty()
                            ? QString::fromLatin1(annot::annotTypeName(type).data(),
                                                  static_cast<qsizetype>(annot::annotTypeName(type).size()))
                            : tool;
    const ToolStyle style = toolStyle(settings, key);
    if (style.color) {
        p.color = *style.color;
    }
    if (style.opacity) {
        p.opacity = *style.opacity;
    }
    if (style.width) {
        p.borderWidth = *style.width;
    } else if (tool == QLatin1String("callout")) {
        p.borderWidth = 1;  // выноска — в рамке (Acrobat), текстовое поле — без
    }
    if (style.icon && !style.icon->empty()) {
        p.icon = *style.icon;
    }
    if (style.font && !style.font->empty()) {
        p.fontName = *style.font;
    }
    if (style.fontSize) {
        p.fontSize = *style.fontSize;
    }
    if (style.textColor) {
        p.textColor = *style.textColor;
    }
    if (style.align) {
        p.align = *style.align;
    }
    if (style.fill) {
        p.interiorColor = *style.fill;
    }
    if (style.lineStart) {
        p.lineStartStyle = static_cast<annot::LineEnding>(*style.lineStart);
    }
    if (style.lineEnd) {
        p.lineEndStyle = static_cast<annot::LineEnding>(*style.lineEnd);
    } else if (tool == QLatin1String("arrow")) {
        p.lineEndStyle = annot::LineEnding::OpenArrow;  // стрелка — с открытой стрелкой на конце
    }
    p.author = author(settings);
    return p;
}

}  // namespace ot::comments
