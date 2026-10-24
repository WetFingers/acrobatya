// ui.xmp_editor — «Дополнительные метаданные» (план 2.5; эталон 3.13 «Метаданные XMP»; view-46, edit-50): из
// «Свойств» — категории Описание / Пользовательские / Дополнительно / Пакет XMP; описание — те же поля, что в
// «Свойствах»; «Дополнительно» — свойства по схемам (dc:title, pdf:Producer…); пакет — x:xmpmeta; своё
// свойство, добавленное в редакторе, после «ОК» — в таблице «Пользовательские», а после «ОК» «Свойств» — в
// /Info и pdfx: пакета XMP; документ без пакета — подсказка «будет создан».
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest/QtTest>

#include "services/app_info.h"
#include "services/document_session.h"
#include "services/props_ops.h"
#include "support/ui_test.h"
#include "ui/dialogs/properties_dialog.h"
#include "ui/dialogs/xmp_editor.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"

using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::PropertiesDialog;
using ot::ui::XmpEditor;

class XmpEditorTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-xmp_editor"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
        QVERIFY(tmp_.isValid());
    }

    void categoriesPacketAndCustomRoundTrip() {
        std::printf("OT_FEATURE:edit-50\nOT_FEATURE:view-46\n");
        const QString path = QDir(tmp_.path()).filePath(QStringLiteral("xmp.pdf"));
        QVERIFY(QFile::copy(QStringLiteral(OTTISK_SOURCE_DIR "/tests/data/mini/blank-a4.pdf"), path));
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        w.resize(1440, 900);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(path));
        PropertiesDialog* d = w.showProperties();
        QTRY_VERIFY_WITH_TIMEOUT(d->isLoaded(), 10000);
        XmpEditor* x = d->showXmpEditor();
        QVERIFY(x->isVisible());
        QCOMPARE(x->categories()->count(), 4);
        QCOMPARE(x->title(), d->titleEdit()->text());
        // «Дополнительно» — схемы пакета; dc:title — среди свойств dc.
        QTreeWidget* tree = x->advancedTree();
        QVERIFY(tree->topLevelItemCount() >= 2);
        bool dcTitle = false;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            for (int j = 0; j < tree->topLevelItem(i)->childCount(); ++j) {
                dcTitle = dcTitle || tree->topLevelItem(i)->child(j)->text(0) == QStringLiteral("dc:title");
            }
        }
        QVERIFY(dcTitle);
        QVERIFY(x->rawView()->toPlainText().contains(QStringLiteral("x:xmpmeta")));
        x->showCategory(XmpEditor::Raw);
        QCOMPARE(x->categories()->currentRow(), static_cast<int>(XmpEditor::Raw));

        x->addCustom(QStringLiteral("Проект"), QStringLiteral("АкроБатя"));
        x->accept();
        QCOMPARE(d->customTable()->rowCount(), 1);
        QCOMPARE(d->customTable()->item(0, 0)->text(), QStringLiteral("Проект"));
        QFuture<void> f = d->applyChanges();
        QTRY_VERIFY_WITH_TIMEOUT(f.isFinished(), 10000);
        QFuture<ot::props::Sheet> sheet = ot::props::readSheet(w.currentDocument()->session());
        QTRY_VERIFY_WITH_TIMEOUT(sheet.isFinished(), 10000);
        QCOMPARE(QString::fromStdString(sheet.result().info.custom.at("Проект")), QStringLiteral("АкроБатя"));
        QVERIFY(QString::fromStdString(sheet.result().xmp).contains(QStringLiteral("pdfx:Проект")));
        d->reject();
    }

    void documentWithoutPacket() {
        XmpEditor x;
        x.load({}, QStringLiteral("Т"), {}, {}, {}, {}, true);
        QVERIFY(x.rawView()->toPlainText().contains(QStringLiteral("будет создан")));
        QCOMPARE(x.advancedTree()->topLevelItemCount(), 0);
        QVERIFY(x.customTable()->editTriggers() == QAbstractItemView::NoEditTriggers);
    }

private:
    QTemporaryDir tmp_;
};

OT_UI_TEST(XmpEditorTest, "xmp_editor");

#include "xmp_editor_test.moc"
