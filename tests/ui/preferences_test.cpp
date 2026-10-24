// ui.preferences — диалог «Установки» (план 2.4b; эталон 3.18, 6.3; view-25, ocr-42): Ctrl+K и ⚙ открывают
// диалог с 16 категориями эталона 3.18 в его порядке; «Общие», «Просмотр страницы», «Документы» — живые,
// остальные — заглушки с версией; «ОК» пишет settings.ini (тема, «Крупные элементы», язык, при запуске, ключи
// просмотра и документов) и применяет тему без перезапуска, «Отмена» ничего не пишет; поля читают ini при
// показе.
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QListWidget>
#include <QPushButton>
#include <QtTest/QtTest>

#include <cstdio>

#include "services/app_info.h"
#include "services/settings.h"
#include "support/ui_test.h"
#include "ui/shell/global_bar.h"
#include "ui/shell/main_window.h"
#include "ui/shell/preferences/documents_page.h"
#include "ui/shell/preferences/general_page.h"
#include "ui/shell/preferences/page_display_page.h"
#include "ui/shell/preferences/preferences_dialog.h"
#include "ui/theme/theme.h"
#include "ui/widgets/empty_state.h"
#include "ui/widgets/number_field.h"

using ot::Settings;
using ot::ui::DocumentsPage;
using ot::ui::GeneralPage;
using ot::ui::MainWindow;
using ot::ui::PageDisplayPage;
using ot::ui::PreferencesDialog;
using ot::ui::Theme;
using ot::ui::ThemeChoice;
namespace keys = ot::settings;

namespace {

void showActive(QWidget& w) {
    w.resize(1440, 900);
    w.show();
    w.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
}

void selectData(QComboBox* box, const QString& value) {
    const int i = box->findData(value);
    QVERIFY2(i >= 0, qPrintable(value));
    box->setCurrentIndex(i);
}

}  // namespace

class PreferencesTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-preferences"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
    }

    void cleanup() {
        QFile::remove(ot::app::settingsPath());
        Theme::apply(*qApp, ThemeChoice::Light, 1.0);  // как у ui_main
    }

    void ctrlKOpensCategoriesInOrder() {
        MainWindow w;
        showActive(w);
        QTest::keyClick(&w, Qt::Key_K, Qt::ControlModifier);
        PreferencesDialog* d = w.preferencesDialog();
        QVERIFY(d != nullptr);
        QTRY_VERIFY(d->isVisible());
        QCOMPARE(
            d->categoryIds(),
            (QStringList{QStringLiteral("general"), QStringLiteral("pageDisplay"),
                         QStringLiteral("documents"), QStringLiteral("commenting"), QStringLiteral("forms"),
                         QStringLiteral("editing"), QStringLiteral("signatures"), QStringLiteral("trust"),
                         QStringLiteral("search"), QStringLiteral("reading"), QStringLiteral("ocr"),
                         QStringLiteral("convert"), QStringLiteral("print"), QStringLiteral("hotkeys"),
                         QStringLiteral("quickbar"), QStringLiteral("about")}));
        QCOMPARE(d->categoryList()->count(), 16);
        QCOMPARE(d->categoryList()->item(0)->text(), QStringLiteral("Общие"));
        QCOMPARE(d->categoryList()->item(1)->text(), QStringLiteral("Просмотр страницы"));
        QCOMPARE(d->categoryList()->item(2)->text(), QStringLiteral("Документы"));
        QCOMPARE(d->categoryList()->item(6)->text(), QStringLiteral("Подписи"));
        QCOMPARE(d->currentCategory(), QStringLiteral("general"));
        // Живые — без этапа (с 2.6 и «Печать», с 3.4 — «Редактирование содержимого», с 4.6 — «Подписи», с 5.1
        // — «Комментирование»), остальные — заглушки «появится в версии 0.N».
        for (const QString& id : d->categoryIds()) {
            const bool live = id == QLatin1String("general") || id == QLatin1String("pageDisplay") ||
                              id == QLatin1String("documents") || id == QLatin1String("print") ||
                              id == QLatin1String("editing") || id == QLatin1String("signatures") ||
                              id == QLatin1String("commenting");
            QCOMPARE(d->page(id)->stage() == 0, live);
        }
        QVERIFY(d->showCategory(QStringLiteral("trust")));
        auto* empty = d->page(QStringLiteral("trust"))->findChild<ot::ui::EmptyState*>();
        QVERIFY(empty != nullptr);
        QCOMPARE(empty->text(), QStringLiteral("Раздел «Диспетчер доверия» появится в версии 0.7."));
        QVERIFY(!d->showCategory(QStringLiteral("nope")));
        // Esc — закрыть без записи.
        QTest::keyClick(d, Qt::Key_Escape);
        QTRY_VERIFY(!d->isVisible());
        QVERIFY(!QFile::exists(ot::app::settingsPath()) || !Settings().contains(keys::kTheme.name));
        // ⚙ глобальной панели — тот же диалог.
        emit w.globalBar().settingsClicked();
        QTRY_VERIFY(w.preferencesDialog()->isVisible());
        QCOMPARE(w.preferencesDialog(), d);
        d->reject();
    }

    void generalAppliesThemeAndLargeElements() {
        std::printf("OT_FEATURE:view-25\n");
        std::printf("OT_FEATURE:ocr-42\n");
        std::fflush(stdout);
        MainWindow w;
        showActive(w);
        PreferencesDialog* d = w.showPreferences(QStringLiteral("general"));
        auto* general = qobject_cast<GeneralPage*>(d->page(QStringLiteral("general")));
        QVERIFY(general != nullptr);
        QCOMPARE(general->themeBox()->currentData().toString(), QStringLiteral("system"));
        QVERIFY(!general->largeElementsBox()->isChecked());
        QCOMPARE(general->themeBox()->count(), 4);
        selectData(general->themeBox(), QStringLiteral("dark"));
        general->largeElementsBox()->setChecked(true);
        general->startupBox()->setCurrentIndex(1);
        general->singleKeyBox()->setChecked(true);
        QTest::mouseClick(d->buttons()->button(QDialogButtonBox::Ok), Qt::LeftButton);
        QTRY_VERIFY(!d->isVisible());
        // Записано в ini и применено без перезапуска.
        const Settings s;
        QCOMPARE(s.get(keys::kTheme), QStringLiteral("dark"));
        QVERIFY(s.get(keys::kLargeElements));
        QVERIFY(s.get(keys::kRestoreSession));
        QVERIFY(s.get(keys::kSingleKeyAccelerators));
        QCOMPARE(Theme::instance().choice(), ThemeChoice::Dark);
        QCOMPARE(Theme::instance().scale(), ot::ui::kLargeElementsScale);
        QCOMPARE(Theme::sizes().globalBar, 60);  // 48 × 1,25
        QCOMPARE(w.globalBar().height(), 60);
        QVERIFY(w.singleKeyAccelerators());
        QVERIFY(MainWindow::restoreSessionEnabled());
        // Повторный показ читает ini.
        d = w.showPreferences();
        QCOMPARE(general->themeBox()->currentData().toString(), QStringLiteral("dark"));
        QVERIFY(general->largeElementsBox()->isChecked());
        d->reject();
    }

    void cancelWritesNothing() {
        MainWindow w;
        showActive(w);
        PreferencesDialog* d = w.showPreferences(QStringLiteral("general"));
        auto* general = qobject_cast<GeneralPage*>(d->page(QStringLiteral("general")));
        selectData(general->themeBox(), QStringLiteral("contrast"));
        QTest::mouseClick(d->buttons()->button(QDialogButtonBox::Cancel), Qt::LeftButton);
        QTRY_VERIFY(!d->isVisible());
        QCOMPARE(Settings().get(keys::kTheme), QStringLiteral("system"));
        QCOMPARE(Theme::instance().choice(), ThemeChoice::Light);
        // Следующий показ — снова из ini, не из брошенных правок.
        w.showPreferences();
        QCOMPARE(general->themeBox()->currentData().toString(), QStringLiteral("system"));
        d->reject();
    }

    void pageDisplayAndDocumentsWriteKeys() {
        MainWindow w;
        showActive(w);
        PreferencesDialog* d = w.showPreferences(QStringLiteral("pageDisplay"));
        QCOMPARE(d->currentCategory(), QStringLiteral("pageDisplay"));
        auto* view = qobject_cast<PageDisplayPage*>(d->page(QStringLiteral("pageDisplay")));
        auto* docs = qobject_cast<DocumentsPage*>(d->page(QStringLiteral("documents")));
        QVERIFY(view != nullptr && docs != nullptr);
        QVERIFY(view->logicalNumbersBox()->isChecked());
        QCOMPARE(view->zoomBox()->currentData().toString(), QStringLiteral("auto"));
        selectData(view->layoutBox(), QStringLiteral("facing"));
        selectData(view->zoomBox(), QStringLiteral("fitWidth"));
        view->smoothTextBox()->setChecked(false);
        view->logicalNumbersBox()->setChecked(false);
        QVERIFY(docs->restoreViewBox()->isChecked());
        QVERIFY(docs->autosaveBox()->isChecked());
        QCOMPARE(docs->autosaveMinutesField()->value(), 5.0);
        docs->autosaveMinutesField()->setValue(12);
        docs->restoreViewBox()->setChecked(false);
        docs->autosaveBox()->setChecked(false);
        QVERIFY(!docs->autosaveMinutesField()->isEnabled());
        d->accept();
        const Settings s;
        QCOMPARE(s.get(keys::kDefaultLayout), QStringLiteral("facing"));
        QCOMPARE(s.get(keys::kDefaultZoom), QStringLiteral("fitWidth"));
        QVERIFY(!s.get(keys::kSmoothText));
        QVERIFY(!s.get(keys::kUseLogicalPageNumbers));
        QCOMPARE(s.get(keys::kAutosaveMinutes), 12);
        QVERIFY(!s.get(keys::kRestoreLastView));
        QVERIFY(!s.get(keys::kAutosave));
        QVERIFY(!MainWindow::restoreLastViewEnabled());  // окно читает тот же ключ
    }
};

OT_UI_TEST(PreferencesTest, "preferences");
#include "preferences_test.moc"
