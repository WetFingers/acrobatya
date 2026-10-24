// ui.properties — диалог «Свойства документа» (план 2.5; эталон 3.1 «Свойства документа», 3.13; view-45,
// view-46, view-47, ocr-32): Ctrl+D открывает его на текущей вкладке; правка Title → /Info и XMP одним шагом
// «Document properties», undo возвращает; пользовательское свойство переживает сохранение и переоткрытие
// (/Info и pdfx: в XMP); начальный вид (панель миниатюр, разворот с прокруткой, страница 3) записан и
// применяется при переоткрытии; /Lang и Base URL пишутся и читаются; «Шрифты» — по первому показу; «Защита»
// зашифрованного — «Защита паролем», «Изменить…» серая до 0.7; не-PDF — только чтение.
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest/QtTest>

#include "services/app_info.h"
#include "services/document_session.h"
#include "services/props_ops.h"
#include "support/ui_test.h"
#include "ui/dialogs/properties_dialog.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/nav_pane.h"
#include "ui/view/page_view.h"

using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::PropertiesDialog;

namespace {

QString source(const char* rel) { return QStringLiteral(OTTISK_SOURCE_DIR "/tests/") + QLatin1String(rel); }

void showActive(QWidget& w) {
    w.resize(1440, 900);
    w.show();
    w.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QTest::qWaitForWindowActive(&w);
}

// Ждать задачу документа с циклом событий (QTRY_* в функции со значением не годится: return без значения).
template <class F>
void spin(const F& future) {
    QElapsedTimer timer;
    timer.start();
    while (!future.isFinished() && timer.elapsed() < 10000) {
        QTest::qWait(10);
    }
}

template <class T>
T wait(QFuture<T> f) {
    spin(f);
    return f.result();
}

void waitVoid(QFuture<void> f) { spin(f); }

PropertiesDialog* openProperties(MainWindow& w, int tab = -1) {
    PropertiesDialog* d = w.showProperties(tab);
    [&] {
        QVERIFY(d != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(d->isLoaded(), 10000);
    }();
    return d;
}

}  // namespace

class PropertiesTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-properties"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
        QVERIFY(tmp_.isValid());
    }

    void ctrlDTitleSyncsInfoAndXmpWithUndo() {
        std::printf("OT_FEATURE:view-45\nOT_FEATURE:view-46\n");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        showActive(w);
        QVERIFY(w.openFile(copy("data/mini/blank-a4.pdf", QStringLiteral("title.pdf"))));
        QTest::keyClick(&w, Qt::Key_D, Qt::ControlModifier);
        QTRY_VERIFY(w.propertiesDialog() != nullptr && w.propertiesDialog()->isVisible());
        PropertiesDialog* d = w.propertiesDialog();
        QTRY_VERIFY_WITH_TIMEOUT(d->isLoaded(), 10000);
        QCOMPARE(d->tabs()->count(), 6);
        QCOMPARE(d->titleEdit()->text(), QStringLiteral("blank-a4"));
        QVERIFY(d->changes().empty());
        d->titleEdit()->setText(QStringLiteral("Годовой отчёт"));
        QVERIFY(d->changes().info.has_value());
        QVERIFY(!d->changes().initial.has_value());  // начальный вид не трогали — не пишется
        waitVoid(d->applyChanges());
        ot::DocumentSession& session = w.currentDocument()->session();
        const ot::props::Sheet sheet = wait(ot::props::readSheet(session));
        QCOMPARE(QString::fromStdString(sheet.info.title), QStringLiteral("Годовой отчёт"));
        QVERIFY(QString::fromStdString(sheet.xmp).contains(QStringLiteral("Годовой отчёт")));
        QTRY_COMPARE(session.undoState().undoName, QStringLiteral("Document properties"));
        QVERIFY(wait(session.undo()));
        QCOMPARE(QString::fromStdString(wait(ot::props::readSheet(session)).info.title),
                 QStringLiteral("blank-a4"));
        d->reject();
    }

    void customPropertySurvivesSaveAndReopen() {
        std::printf("OT_FEATURE:view-46\n");
        const QString path = copy("data/mini/blank-a4.pdf", QStringLiteral("custom.pdf"));
        {
            MainWindow w;
            showActive(w);
            QVERIFY(w.openFile(path));
            PropertiesDialog* d = openProperties(w, PropertiesDialog::Custom);
            QCOMPARE(d->tabs()->currentIndex(), static_cast<int>(PropertiesDialog::Custom));
            d->addCustom(QStringLiteral("Отдел"), QStringLiteral("Бухгалтерия"));
            d->accept();  // запись идёт задачей документа; диалог закрыт и удалён
            ot::DocumentSession& session = w.currentDocument()->session();
            QTRY_VERIFY_WITH_TIMEOUT(session.undoState().canUndo, 10000);
            QVERIFY(wait(session.save()).ok());
            QTRY_VERIFY(!w.currentDocument()->isModified());  // «изменён» снимает сигнал saved из очереди
            QVERIFY(w.closeDocument(0));
        }
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(path));
        const ot::props::Sheet sheet = wait(ot::props::readSheet(w.currentDocument()->session()));
        QCOMPARE(sheet.info.custom.count("Отдел"), std::size_t{1});
        QCOMPARE(QString::fromStdString(sheet.info.custom.at("Отдел")), QStringLiteral("Бухгалтерия"));
        QVERIFY(QString::fromStdString(sheet.xmp).contains(QStringLiteral("pdfx:Отдел")));
        PropertiesDialog* d = openProperties(w, PropertiesDialog::Custom);
        QCOMPARE(d->customTable()->rowCount(), 1);
        QCOMPARE(d->customTable()->item(0, 0)->text(), QStringLiteral("Отдел"));
        d->reject();
    }

    void initialViewWrittenAndAppliedOnReopen() {
        std::printf("OT_FEATURE:view-47\n");
        const QString path = copy("fixtures/demo/report.pdf", QStringLiteral("initial.pdf"));
        {
            MainWindow w;
            showActive(w);
            QVERIFY(w.openFile(path));
            PropertiesDialog* d = openProperties(w, PropertiesDialog::InitialViewTab);
            QCOMPARE(d->navigationBox()->currentData().toString(), QStringLiteral("UseOutlines"));
            d->navigationBox()->setCurrentIndex(d->navigationBox()->findData(QStringLiteral("UseThumbs")));
            d->layoutBox()->setCurrentIndex(d->layoutBox()->findData(QStringLiteral("TwoColumnLeft")));
            d->openPageBox()->setValue(3);
            d->magnificationBox()->setCurrentIndex(1);  // «Уместить страницу»
            d->showTitleBox()->setCurrentIndex(1);
            const ot::props::Changes c = d->changes();
            QVERIFY(c.initial.has_value());
            QVERIFY(!c.info.has_value());
            waitVoid(d->applyChanges());
            d->reject();
            QVERIFY(wait(w.currentDocument()->session().save()).ok());
            QTRY_VERIFY(!w.currentDocument()->isModified());
            QVERIFY(w.closeDocument(0));
        }
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(path));
        DocumentView* doc = w.currentDocument();
        QTRY_COMPARE(doc->view().currentPage(), 2);  // /OpenAction — страница 3
        QVERIFY(w.navPane().isPanelOpen());
        QCOMPARE(w.navPane().currentPane(), QStringLiteral("thumbnails"));     // /PageMode /UseThumbs
        QCOMPARE(doc->view().pageMode(), ot::ui::PageMode::FacingContinuous);  // /PageLayout /TwoColumnLeft
        const ot::props::Sheet sheet = wait(ot::props::readSheet(doc->session()));
        QVERIFY(sheet.initial.displayDocTitle);
        QVERIFY(w.windowTitle().startsWith(QString::fromStdString(sheet.info.title)));
    }

    void languageAndBaseUrl() {
        std::printf("OT_FEATURE:ocr-32\nOT_FEATURE:view-45\n");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        showActive(w);
        QVERIFY(w.openFile(copy("data/mini/blank-a4.pdf", QStringLiteral("lang.pdf"))));
        PropertiesDialog* d = openProperties(w, PropertiesDialog::Advanced);
        QCOMPARE(d->languageBox()->currentData().toString(), QStringLiteral("ru-RU"));
        d->languageBox()->setCurrentIndex(d->languageBox()->findData(QStringLiteral("en-US")));
        d->baseUrlEdit()->setText(QStringLiteral("https://example.org/base/"));
        const ot::props::Changes c = d->changes();
        QVERIFY(c.lang.has_value());
        QVERIFY(c.baseUrl.has_value());
        waitVoid(d->applyChanges());
        d->reject();
        ot::props::Sheet sheet = wait(ot::props::readSheet(w.currentDocument()->session()));
        QCOMPARE(QString::fromStdString(sheet.lang), QStringLiteral("en-US"));
        QCOMPARE(QString::fromStdString(sheet.baseUrl), QStringLiteral("https://example.org/base/"));
        // Свой код языка вводом.
        d = openProperties(w, PropertiesDialog::Advanced);
        QCOMPARE(d->languageBox()->currentData().toString(), QStringLiteral("en-US"));
        d->languageBox()->setEditText(QStringLiteral("kk-KZ"));
        waitVoid(d->applyChanges());
        d->reject();
        sheet = wait(ot::props::readSheet(w.currentDocument()->session()));
        QCOMPARE(QString::fromStdString(sheet.lang), QStringLiteral("kk-KZ"));
    }

    void fontsSecurityAndCheck() {
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(source("fixtures/demo/report.pdf")));
        PropertiesDialog* d = openProperties(w);
        QVERIFY(!d->fontsLoaded());  // только по первому показу вкладки
        d->showTab(PropertiesDialog::Fonts);
        QTRY_VERIFY_WITH_TIMEOUT(d->fontsLoaded(), 10000);
        QVERIFY(d->fontsTree()->topLevelItemCount() > 0);
        QVERIFY(d->fontsTree()->topLevelItem(0)->childCount() >= 2);  // тип, страницы
        QCOMPARE(d->securityMethodLabel()->text(), QStringLiteral("Нет защиты"));
        QVERIFY(!d->changeSecurityButton()->isEnabled());
        QVERIFY(d->changeSecurityButton()->toolTip().contains(QStringLiteral("0.7")));
        QSignalSpy checked(d, &PropertiesDialog::checked);
        d->checkButton()->click();
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, 10000);
        QVERIFY(!d->warningsView()->toPlainText().isEmpty());
        d->reject();

        QVERIFY(w.openFile(source("fixtures/enc/enc-aes-256-r6.pdf"), -1, QStringLiteral("user")));
        d = openProperties(w, PropertiesDialog::Security);
        QCOMPARE(d->securityMethodLabel()->text(), QStringLiteral("Защита паролем"));
        QVERIFY(!d->sheet().doc.encryption.empty());
        d->reject();
    }

    void viewOnlyDocumentIsReadOnly() {
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(source("fixtures/images/logo.png")));
        PropertiesDialog* d = openProperties(w);
        QVERIFY(d->titleEdit()->isReadOnly());
        QVERIFY(!d->navigationBox()->isEnabled());
        d->titleEdit()->setText(QStringLiteral("x"));
        QVERIFY(d->changes().empty());
        d->reject();
    }

private:
    QString copy(const char* rel, const QString& name) {
        const QString target = QDir(tmp_.path()).filePath(name);
        QFile::remove(target);
        if (!QFile::copy(source(rel), target)) {
            return {};
        }
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        return target;
    }

    QTemporaryDir tmp_;
};

OT_UI_TEST(PropertiesTest, "properties");

#include "properties_test.moc"
