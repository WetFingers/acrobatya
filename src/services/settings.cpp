#include "services/settings.h"

#include <QDir>

#include <algorithm>

#include <spdlog/spdlog.h>

#include "services/app_info.h"

namespace ot {

Settings::Settings() : Settings(app::settingsPath()) {}

Settings::Settings(const QString& iniPath) : ini_(iniPath, QSettings::IniFormat) {}

bool Settings::get(const settings::BoolKey& key) const {
    const QVariant v = ini_.value(QLatin1String(key.name));
    if (!v.isValid()) {
        return key.def;
    }
    // INI хранит строки: «true»/«false», а у ключей, записанных вручную, — «1»/«0».
    const QString s = v.toString().trimmed().toLower();
    if (s == QLatin1String("true") || s == QLatin1String("1")) {
        return true;
    }
    if (s == QLatin1String("false") || s == QLatin1String("0")) {
        return false;
    }
    return key.def;
}

int Settings::get(const settings::IntKey& key) const {
    bool ok = false;
    const int v = ini_.value(QLatin1String(key.name)).toInt(&ok);
    return ok && v >= key.min && v <= key.max ? v : key.def;
}

QString Settings::get(const settings::StringKey& key) const {
    const QVariant v = ini_.value(QLatin1String(key.name));
    if (!v.isValid()) {
        return QString::fromUtf8(key.def);
    }
    const QString s = v.toString();
    return allowed(key, s) ? s : QString::fromUtf8(key.def);
}

QStringList Settings::get(const settings::ListKey& key) const {
    return ini_.value(QLatin1String(key.name)).toStringList();
}

void Settings::set(const settings::BoolKey& key, bool value) {
    ini_.setValue(QLatin1String(key.name), value);
}

void Settings::set(const settings::IntKey& key, int value) {
    ini_.setValue(QLatin1String(key.name), std::clamp(value, key.min, key.max));
}

void Settings::set(const settings::StringKey& key, const QString& value) {
    if (!allowed(key, value)) {
        spdlog::warn("settings: {} = '{}' is not one of '{}' — default kept", key.name, value.toStdString(),
                     key.choices);
        ini_.remove(QLatin1String(key.name));
        return;
    }
    ini_.setValue(QLatin1String(key.name), value);
}

void Settings::set(const settings::ListKey& key, const QStringList& value) {
    ini_.setValue(QLatin1String(key.name), value);
}

bool Settings::contains(const char* key) const { return ini_.contains(QLatin1String(key)); }

void Settings::reset(const char* key) { ini_.remove(QLatin1String(key)); }

void Settings::sync() { ini_.sync(); }

QString Settings::fileName() const { return ini_.fileName(); }

bool Settings::allowed(const settings::StringKey& key, const QString& value) {
    if (key.choices == nullptr || key.choices[0] == '\0') {
        return true;
    }
    const QStringList choices = QString::fromUtf8(key.choices).split(QLatin1Char('|'));
    return choices.contains(value);
}

void Settings::installDefaultFormat() {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    // Путь INI по умолчанию — каталог данных (ASCII): QSettings("QtProject") → <данные>\QtProject.ini.
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, app::dataDir());
}

int Settings::version() const { return get(settings::kMetaVersion); }

bool Settings::migrate() {
    const int from = version();
    if (from >= kVersion) {
        return true;
    }
    // 0 → 1 (2.4b): ключи 2.3–2.4 уже на своих местах (settings_keys.h повторяет их) — переносить нечего.
    set(settings::kMetaVersion, kVersion);
    ini_.sync();
    if (ini_.status() != QSettings::NoError) {
        spdlog::warn("settings: cannot write {} (migration {} -> {})", ini_.fileName().toStdString(), from,
                     kVersion);
        return false;
    }
    spdlog::info("settings: {} migrated {} -> {}", QDir::toNativeSeparators(ini_.fileName()).toStdString(),
                 from, kVersion);
    return true;
}

}  // namespace ot
