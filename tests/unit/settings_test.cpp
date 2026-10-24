// unit.settings.* — настройки программы (план 2.4b; эталон 4.7, 3.18): Settings поверх settings.ini даёт
// умолчания из settings_keys.h без файла, пишет и читает INI (строки «true»/«5»), чужие значения — умолчание,
// числа — в пределах, миграция по meta/version; формат QSettings по умолчанию — INI в каталоге данных, так
// что и чужой QSettings без формата (диалоги Qt) не идёт в реестр (страж unit.guard.no_registry — по
// исходникам).
#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

#include <filesystem>

#include "services/app_info.h"
#include "services/settings.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
namespace keys = ot::settings;
using ot::Settings;

namespace {

QString iniIn(const ot::test::TempDir& dir) {
    return QString::fromStdWString((dir.path() / L"settings.ini").wstring());
}

QByteArray fileText(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// OTTISK_DATA_DIR на время теста: каталог данных — временный, а не %LOCALAPPDATA%\AcroBatya человека. Путь —
// ASCII, как у каталога данных программы (эталон 4.7): qputenv на Windows передаёт байты в кодировке ANSI.
class ScopedDataDir {
public:
    explicit ScopedDataDir(const QString& dir) : saved_(qgetenv("OTTISK_DATA_DIR")) {
        qputenv("OTTISK_DATA_DIR", dir.toLocal8Bit());
    }
    ~ScopedDataDir() { qputenv("OTTISK_DATA_DIR", saved_); }
    ScopedDataDir(const ScopedDataDir&) = delete;
    ScopedDataDir& operator=(const ScopedDataDir&) = delete;

private:
    QByteArray saved_;
};

}  // namespace

TEST(settings, defaults_without_file) {
    ot::test::qtApp();
    ot::test::TempDir dir;
    const Settings s(iniIn(dir));
    EXPECT_EQ(s.get(keys::kTheme), QStringLiteral("system"));
    EXPECT_FALSE(s.get(keys::kLargeElements));
    EXPECT_FALSE(s.get(keys::kRestoreSession));
    EXPECT_EQ(s.get(keys::kLanguage), QStringLiteral("system"));
    EXPECT_TRUE(s.get(keys::kRestoreLastView));
    EXPECT_TRUE(s.get(keys::kUseLogicalPageNumbers));
    EXPECT_EQ(s.get(keys::kAutosaveMinutes), 5);
    EXPECT_EQ(s.get(keys::kDefaultLayout), QStringLiteral("auto"));
    EXPECT_TRUE(s.get(keys::kPinnedTools).isEmpty());
    EXPECT_EQ(s.version(), 0);
    EXPECT_FALSE(fs::exists(dir.path() / L"settings.ini")) << "reading must not create the file";
}

TEST(settings, write_and_read_back_ini) {
    ot::test::qtApp();
    ot::test::TempDir dir;
    const QString path = iniIn(dir);
    {
        Settings s(path);
        s.set(keys::kTheme, QStringLiteral("dark"));
        s.set(keys::kLargeElements, true);
        s.set(keys::kAutosaveMinutes, 99);  // вне 1–30 — к пределу
        s.set(keys::kPinnedTools, {QStringLiteral("edit"), QStringLiteral("comment")});
        s.set(keys::kLanguage, QStringLiteral("en"));
        s.sync();
        EXPECT_EQ(s.raw().status(), QSettings::NoError);
        EXPECT_EQ(QDir::cleanPath(s.fileName()), QDir::cleanPath(path));
    }
    const Settings again(path);
    EXPECT_EQ(again.get(keys::kTheme), QStringLiteral("dark"));
    EXPECT_TRUE(again.get(keys::kLargeElements));
    EXPECT_EQ(again.get(keys::kAutosaveMinutes), 30);
    EXPECT_EQ(again.get(keys::kPinnedTools),
              (QStringList{QStringLiteral("edit"), QStringLiteral("comment")}));
    EXPECT_EQ(again.get(keys::kLanguage), QStringLiteral("en"));
    const QByteArray text = fileText(path);
    // Группу «general» Qt пишет как [%General]: [General] в INI Qt — ключи без группы.
    EXPECT_TRUE(text.contains("[%General]")) << text.constData();
    EXPECT_TRUE(text.contains("theme=dark")) << text.constData();
    EXPECT_TRUE(text.contains("largeElements=true")) << text.constData();
    EXPECT_TRUE(text.contains("autosaveMinutes=30")) << text.constData();
}

TEST(settings, foreign_values_fall_back_to_defaults) {
    ot::test::qtApp();
    ot::test::TempDir dir;
    const QString path = iniIn(dir);
    {
        QSettings raw(path, QSettings::IniFormat);
        raw.setValue(QStringLiteral("general/theme"), QStringLiteral("purple"));
        raw.setValue(QStringLiteral("documents/autosaveMinutes"), QStringLiteral("abc"));
        raw.setValue(QStringLiteral("documents/restoreLastView"), QStringLiteral("maybe"));
        raw.setValue(QStringLiteral("general/largeElements"), QStringLiteral("1"));  // записано руками
    }
    Settings s(path);
    EXPECT_EQ(s.get(keys::kTheme), QStringLiteral("system"));
    EXPECT_EQ(s.get(keys::kAutosaveMinutes), 5);
    EXPECT_TRUE(s.get(keys::kRestoreLastView));
    EXPECT_TRUE(s.get(keys::kLargeElements));
    s.set(keys::kTheme, QStringLiteral("neon"));  // не из choices — ключ убран, действует умолчание
    EXPECT_FALSE(s.contains(keys::kTheme.name));
    s.set(keys::kTheme, QStringLiteral("contrast"));
    EXPECT_EQ(s.get(keys::kTheme), QStringLiteral("contrast"));
    s.reset(keys::kTheme.name);
    EXPECT_EQ(s.get(keys::kTheme), QStringLiteral("system"));
}

TEST(settings, string_defaults_are_allowed_choices) {
    for (const keys::StringKey* k :
         {&keys::kTheme, &keys::kLanguage, &keys::kDefaultLayout, &keys::kDefaultZoom, &keys::kNavPaneCurrent,
          &keys::kFullScreenDefaultTransition, &keys::kFullScreenBackground, &keys::kProtectedView,
          &keys::kInternetAccess}) {
        EXPECT_TRUE(Settings::allowed(*k, QString::fromUtf8(k->def))) << k->name;
    }
    for (const keys::IntKey* k : {&keys::kAutosaveMinutes, &keys::kMetaVersion, &keys::kNavPaneWidth,
                                  &keys::kFullScreenAdvanceSeconds}) {
        EXPECT_TRUE(k->def >= k->min && k->def <= k->max) << k->name;
    }
}

TEST(settings, migration_writes_meta_version_once) {
    ot::test::qtApp();
    ot::test::TempDir dir;
    const QString path = iniIn(dir);
    {
        QSettings raw(path, QSettings::IniFormat);
        raw.setValue(QStringLiteral("general/restoreSession"), true);  // файл 2.3 — без meta/version
    }
    Settings s(path);
    EXPECT_EQ(s.version(), 0);
    EXPECT_TRUE(s.migrate());
    EXPECT_EQ(s.version(), Settings::kVersion);
    EXPECT_TRUE(s.get(keys::kRestoreSession)) << "migration keeps existing keys";
    EXPECT_TRUE(s.migrate());
    EXPECT_TRUE(fileText(path).contains("[meta]"));
}

TEST(settings, default_format_is_ini_in_data_dir_not_registry) {
    ot::test::qtApp();
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const ScopedDataDir data(dir.path());
    Settings::installDefaultFormat();
    EXPECT_EQ(QSettings::defaultFormat(), QSettings::IniFormat);
    // QSettings без формата и пути (конструкторы QSettings() и QSettings(scope) берут defaultFormat; с
    // организацией в аргументах Qt всегда берёт NativeFormat — такой QSettings тест не создаёт: он открыл бы
    // реестр).
    const QSettings plain(QSettings::UserScope);
    EXPECT_EQ(plain.format(), QSettings::IniFormat);
    EXPECT_TRUE(plain.fileName().endsWith(QStringLiteral(".ini"), Qt::CaseInsensitive))
        << plain.fileName().toStdString();
    EXPECT_TRUE(
        QDir::cleanPath(plain.fileName()).startsWith(QDir::cleanPath(dir.path()), Qt::CaseInsensitive))
        << plain.fileName().toStdString();
    EXPECT_FALSE(plain.fileName().contains(QStringLiteral("HKEY_"), Qt::CaseInsensitive));
    // Settings() без пути — settings.ini каталога данных.
    const Settings s;
    EXPECT_EQ(QDir::cleanPath(s.fileName()), QDir::cleanPath(ot::app::settingsPath()));
    EXPECT_TRUE(s.fileName().endsWith(QStringLiteral("settings.ini")));
}
