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
    }
    if (style.icon && !style.icon->empty()) {
        p.icon = *style.icon;
    }
    p.author = author(settings);
    return p;
}

}  // namespace ot::comments
