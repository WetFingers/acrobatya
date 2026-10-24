#include "services/sign_settings.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "services/settings.h"

namespace ot {

namespace sign_settings {

const char* graphicName(sign::AppearanceGraphic g) {
    switch (g) {
        case sign::AppearanceGraphic::None:
            return "none";
        case sign::AppearanceGraphic::Image:
            return "image";
        case sign::AppearanceGraphic::Name:
            break;
    }
    return "name";
}

sign::AppearanceGraphic graphicFromName(const QString& name) {
    if (name == QLatin1String("none")) {
        return sign::AppearanceGraphic::None;
    }
    if (name == QLatin1String("image")) {
        return sign::AppearanceGraphic::Image;
    }
    return sign::AppearanceGraphic::Name;
}

QString pathText(const std::filesystem::path& p) { return QString::fromStdWString(p.wstring()); }

std::filesystem::path textPath(const QString& s) { return std::filesystem::path(s.toStdWString()); }

}  // namespace sign_settings

QJsonObject appearanceToJson(const NamedAppearance& appearance) {
    const sign::SignatureAppearance& a = appearance.look;
    QJsonObject text;
    text.insert(QStringLiteral("name"), a.text.name);
    text.insert(QStringLiteral("dn"), a.text.dn);
    text.insert(QStringLiteral("reason"), a.text.reason);
    text.insert(QStringLiteral("location"), a.text.location);
    text.insert(QStringLiteral("date"), a.text.date);
    text.insert(QStringLiteral("labels"), a.text.labels);
    QJsonObject o;
    o.insert(QStringLiteral("name"), appearance.name);
    o.insert(QStringLiteral("graphic"), QLatin1String(sign_settings::graphicName(a.graphic)));
    o.insert(QStringLiteral("image"), sign_settings::pathText(a.image));
    o.insert(QStringLiteral("imageOnly"), a.imageOnly);
    o.insert(QStringLiteral("text"), text);
    o.insert(QStringLiteral("logo"), sign_settings::pathText(a.logo));
    o.insert(QStringLiteral("language"), QString::fromStdString(a.language));
    return o;
}

NamedAppearance appearanceFromJson(const QJsonObject& o) {
    NamedAppearance out;
    out.name = o.value(QLatin1String("name")).toString();
    sign::SignatureAppearance& a = out.look;
    a.graphic = sign_settings::graphicFromName(o.value(QLatin1String("graphic")).toString());
    a.image = sign_settings::textPath(o.value(QLatin1String("image")).toString());
    a.imageOnly = o.value(QLatin1String("imageOnly")).toBool(false);
    const QJsonObject text = o.value(QLatin1String("text")).toObject();
    a.text.name = text.value(QLatin1String("name")).toBool(a.text.name);
    a.text.dn = text.value(QLatin1String("dn")).toBool(a.text.dn);
    a.text.reason = text.value(QLatin1String("reason")).toBool(a.text.reason);
    a.text.location = text.value(QLatin1String("location")).toBool(a.text.location);
    a.text.date = text.value(QLatin1String("date")).toBool(a.text.date);
    a.text.labels = text.value(QLatin1String("labels")).toBool(a.text.labels);
    a.logo = sign_settings::textPath(o.value(QLatin1String("logo")).toString());
    const QString language = o.value(QLatin1String("language")).toString();
    if (!language.isEmpty()) {
        a.language = language.toStdString();
    }
    return out;
}

std::vector<NamedAppearance> loadAppearances(const Settings& settings) {
    std::vector<NamedAppearance> out;
    const QJsonDocument doc = QJsonDocument::fromJson(settings.get(settings::kSignAppearances).toUtf8());
    for (const QJsonValue& v : doc.array()) {
        NamedAppearance a = appearanceFromJson(v.toObject());
        if (!a.name.isEmpty()) {
            out.push_back(std::move(a));
        }
    }
    return out;
}

void saveAppearances(Settings& settings, const std::vector<NamedAppearance>& appearances) {
    QJsonArray array;
    for (const NamedAppearance& a : appearances) {
        array.append(appearanceToJson(a));
    }
    settings.set(settings::kSignAppearances,
                 QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

}  // namespace ot
