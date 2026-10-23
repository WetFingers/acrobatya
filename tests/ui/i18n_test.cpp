// ui.i18n — каркас переводов (эталон 5.7, основа cloud-60): .qm встроены ресурсом :/i18n с qtbase, смена
// языка меняет заголовок уже открытого окна (LanguageChange), выбор языка: --lang → настройка → система.
#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cstdio>

#include "services/i18n.h"
#include "support/ui_test.h"
#include "ui/shell/main_window.h"

class I18nTest : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { ot::i18n::install(QStringLiteral("ru")); }

    void translationsAreEmbedded() {
        QVERIFY(QFile::exists(QStringLiteral(":/i18n/ottisk_ru.qm")));
        QVERIFY(QFile::exists(QStringLiteral(":/i18n/ottisk_en.qm")));
        QCOMPARE(ot::i18n::available(), (QStringList{QStringLiteral("ru"), QStringLiteral("en")}));
    }

    // Переключение языка меняет заголовок окна — и нового, и уже показанного.
    void switchingLanguageChangesWindowTitle() {
        QVERIFY(ot::i18n::install(QStringLiteral("ru")));
        ot::ui::MainWindow w;
        w.show();
        QCOMPARE(w.windowTitle(), QStringLiteral("АкроБатя"));
        QVERIFY(ot::i18n::install(QStringLiteral("en")));
        QCOMPARE(ot::i18n::current(), QStringLiteral("en"));
        QTRY_COMPARE(w.windowTitle(), QStringLiteral("AcroBatya"));
        ot::ui::MainWindow fresh;
        QCOMPARE(fresh.windowTitle(), QStringLiteral("AcroBatya"));
        QVERIFY(ot::i18n::install(QStringLiteral("ru")));
        QTRY_COMPARE(w.windowTitle(), QStringLiteral("АкроБатя"));
        std::printf("OT_FEATURE:cloud-60\n");
    }

    // qtbase_<язык> внутри нашего .qm (MERGE_QT_TRANSLATIONS): стандартные кнопки Qt — по-русски.
    void qtbaseIsMerged() {
        ot::i18n::install(QStringLiteral("ru"));
        QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Cancel"), QStringLiteral("Отмена"));
        QCOMPARE(QLocale().language(), QLocale::Russian);
        ot::i18n::install(QStringLiteral("en"));
        QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Cancel"), QStringLiteral("Cancel"));
        QCOMPARE(QLocale().language(), QLocale::English);
    }

    void resolvesRequestAndSettings() {
        QCOMPARE(ot::i18n::resolve(QStringLiteral("ru")), QStringLiteral("ru"));
        QCOMPARE(ot::i18n::resolve(QStringLiteral("EN")), QStringLiteral("en"));
        const QString system =
            QLocale::system().language() == QLocale::Russian ? QStringLiteral("ru") : QStringLiteral("en");
        QCOMPARE(ot::i18n::resolve(QStringLiteral("system")), system);
        QCOMPARE(ot::i18n::resolve(QString()), system);
        QCOMPARE(ot::i18n::resolve(QStringLiteral("de")), system);
        QTemporaryDir dir;
        const QString ini = dir.filePath(QStringLiteral("settings.ini"));
        QCOMPARE(ot::i18n::fromSettings(ini), QStringLiteral("system"));  // файла нет
        {
            QSettings s(ini, QSettings::IniFormat);
            s.setValue(QLatin1String(ot::i18n::kLanguageKey), QStringLiteral("en"));
        }
        QCOMPARE(ot::i18n::fromSettings(ini), QStringLiteral("en"));
    }
};

OT_UI_TEST(I18nTest, "i18n");
#include "i18n_test.moc"
