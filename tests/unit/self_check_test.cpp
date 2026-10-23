// unit.self_check.* — AcroBatya.exe --self-check без QPA (план 2.4b; эталон 3.18;
// tests/build/install_test.py): с QT_QPA_PLATFORM=bogus код 0 и строки version, qt, mupdf, data, plugins,
// render, tessdata, qpdf, translations, association, «result: ok», в stderr нет «Could not load the Qt
// platform plugin»; плагины проверяются по файлам; tessdata — настоящей загрузкой языка (рядом с программой
// или TESSDATA_PREFIX); самопроверка в процессе unit_tests (QCoreApplication, без ресурсов :/i18n) честно
// называет недостающие переводы и не трогает QPA.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#include <filesystem>

#include "core/ocr/tessdata.h"
#include "services/app_info.h"
#include "support/qt_app.h"

namespace fs = std::filesystem;

namespace {

QString exePath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("AcroBatya.exe"));
}

void touch(const QString& path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
}

}  // namespace

TEST(self_check, exe_passes_with_bogus_qpa_platform) {
    ot::test::qtApp();
    const QString exe = exePath();
    if (!QFileInfo::exists(exe)) {
        GTEST_SKIP() << "AcroBatya.exe is not built next to unit_tests";
    }
    if (ot::ocr::findTessdata(QCoreApplication::applicationDirPath().toStdWString()).empty()) {
        GTEST_SKIP() << "no tessdata next to the build and no TESSDATA_PREFIX";
    }
    QTemporaryDir data;
    ASSERT_TRUE(data.isValid());
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("bogus"));  // QPA не нужна вовсе
    env.insert(QStringLiteral("OTTISK_DATA_DIR"), data.path());
    QProcess p;
    p.setProcessEnvironment(env);
    p.start(exe, {QStringLiteral("--self-check")});
    ASSERT_TRUE(p.waitForFinished(120000));
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    const QString err = QString::fromUtf8(p.readAllStandardError());
    EXPECT_EQ(p.exitCode(), 0) << out.toStdString() << err.toStdString();
    EXPECT_FALSE(err.contains(QStringLiteral("Could not load the Qt platform plugin"))) << err.toStdString();
    for (const char* key : {"version: ", "qt: ", "mupdf: ", "data: ", "plugins: ", "render: ", "tessdata: ",
                            "qpdf: ", "translations: ru, en", "association: ", "result: ok"}) {
        EXPECT_TRUE(out.contains(QLatin1String(key))) << key << "\n" << out.toStdString();
    }
    EXPECT_FALSE(out.contains(QStringLiteral("FAILED"))) << out.toStdString();
    EXPECT_TRUE(out.contains(QStringLiteral("rus"))) << "tessdata languages are listed";
}

TEST(self_check, plugins_are_checked_by_files) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QTemporaryDir other;
    ASSERT_TRUE(other.isValid());
    const QStringList dirs{dir.path(), other.path()};
    EXPECT_EQ(ot::app::missingPlugins(dirs), ot::app::requiredPlugins());
    touch(QDir(dir.path()).filePath(QStringLiteral("platforms/qwindows.dll")));
    EXPECT_EQ(ot::app::missingPlugins(dirs), QStringList{QStringLiteral("platforms/qoffscreen.dll")});
    touch(QDir(other.path()).filePath(QStringLiteral("platforms/qoffscreen.dll")));  // в любом из каталогов
    EXPECT_TRUE(ot::app::missingPlugins(dirs).isEmpty());
    // Каталоги поиска этой сборки (qt_runtime кладёт platforms/ рядом с exe) — полные.
    ot::test::qtApp();
    EXPECT_TRUE(ot::app::missingPlugins(ot::app::pluginsDirs()).isEmpty())
        << ot::app::pluginsDirs().join(QLatin1Char(';')).toStdString();
}

TEST(self_check, tessdata_is_loaded_not_just_found) {
    QTemporaryDir empty;
    ASSERT_TRUE(empty.isValid());
    const ot::ocr::TessdataProbe none = ot::ocr::probeTessdata({});
    EXPECT_FALSE(none.ok);
    EXPECT_FALSE(none.error.empty());
    const ot::ocr::TessdataProbe noLang = ot::ocr::probeTessdata(empty.path().toStdWString(), "rus");
    EXPECT_FALSE(noLang.ok);
    EXPECT_NE(noLang.error.find("rus.traineddata"), std::string::npos) << noLang.error;
    // Пустой файл с нужным именем — найден, но не загружается.
    touch(QDir(empty.path()).filePath(QStringLiteral("rus.traineddata")));
    const ot::ocr::TessdataProbe broken = ot::ocr::probeTessdata(empty.path().toStdWString(), "rus");
    EXPECT_FALSE(broken.ok);
    EXPECT_EQ(broken.languages, std::vector<std::string>{"rus"});
    const fs::path real = ot::ocr::findTessdata(QCoreApplication::applicationDirPath().toStdWString());
    if (real.empty()) {
        GTEST_SKIP() << "no tessdata next to the build and no TESSDATA_PREFIX";
    }
    const ot::ocr::TessdataProbe good = ot::ocr::probeTessdata(real, "rus");
    EXPECT_TRUE(good.ok) << good.error;
    EXPECT_NE(std::find(good.languages.begin(), good.languages.end(), "eng"), good.languages.end());
}

TEST(self_check, in_process_check_needs_no_qpa_and_names_missing_translations) {
    ot::test::qtApp();  // QCoreApplication — без QPA
    const QByteArray saved = qgetenv("QT_QPA_PLATFORM");
    qputenv("QT_QPA_PLATFORM", "bogus");
    QStringList lines;
    const bool ok = ot::app::selfCheck(lines);
    qputenv("QT_QPA_PLATFORM", saved);
    const QString text = lines.join(QLatin1Char('\n'));
    // В unit_tests нет ресурса :/i18n (он у AcroBatya и ui_tests) — проверка честно красная по переводам.
    EXPECT_FALSE(ok);
    EXPECT_TRUE(text.contains(QStringLiteral("translations: missing :/i18n/ottisk_ru.qm")))
        << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("render: 595x842"))) << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("result: FAILED")));
}
