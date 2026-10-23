// Настройки программы (эталон 4.7, 3.18): QSettings(IniFormat) поверх %LOCALAPPDATA%\AcroBatya\settings.ini
// (OTTISK_DATA_DIR — тесты и снимки), никогда не реестр (страж unit.guard.no_registry). Ключи и умолчания —
// services/settings_keys.h; Settings читает их типизированно (чужое значение — умолчание) и пишет сразу в
// файл. Объект дешёвый: QSettings одного файла делят кеш внутри Qt, поэтому Settings создаётся там, где
// нужен, а не живёт синглтоном. Поток: любой (каждый поток — свой объект).
#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "services/settings_keys.h"

namespace ot {

class Settings {
public:
    // Версия схемы: шаги миграции — в Settings::migrate (settings.cpp).
    static constexpr int kVersion = 1;

    Settings();  // app::settingsPath()
    explicit Settings(const QString& iniPath);
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    bool get(const settings::BoolKey& key) const;
    int get(const settings::IntKey& key) const;
    QString get(const settings::StringKey& key) const;
    QStringList get(const settings::ListKey& key) const;

    void set(const settings::BoolKey& key, bool value);
    void set(const settings::IntKey& key, int value);  // вне пределов — std::clamp
    void set(const settings::StringKey& key, const QString& value);
    void set(const settings::ListKey& key, const QStringList& value);

    bool contains(const char* key) const;
    // Убрать ключ (или группу) — снова действует умолчание.
    void reset(const char* key);
    void sync();
    QString fileName() const;
    // Сам QSettings — для групп с ключами по id (hotkeys/<id>) и чтения без типа.
    QSettings& raw() { return ini_; }
    const QSettings& raw() const { return ini_; }

    // Допустимо ли значение строкового ключа (choices).
    static bool allowed(const settings::StringKey& key, const QString& value);

    // Формат QSettings по умолчанию — INI в каталоге данных (QSettings::setDefaultFormat + setPath):
    // QSettings() и QSettings(scope) без формата тоже не идут в реестр. QSettings(организация, …) Qt
    // открывает в NativeFormat всегда — в src его нет (страж unit.guard.no_registry). Зовётся в начале main,
    // до первого QSettings.
    static void installDefaultFormat();

    int version() const;  // meta/version (0 — файл до 2.4b или нового нет)
    // Миграции по meta/version до kVersion; false — файл не записать (только чтение, диск).
    bool migrate();

private:
    QSettings ini_;
};

}  // namespace ot
