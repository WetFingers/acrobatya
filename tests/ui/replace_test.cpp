// ui.replace — «Найти и заменить» в панели «Найти» (план 3.4, п. 4; эталон 3.9 «Найти и заменить», 6.8
// Ctrl+Shift+H; edit-05, view-18): find.pdf — «печать» 12 раз на трёх страницах; Ctrl+Shift+H открывает
// панель с полем «Заменить на»; «Заменить» меняет текущее совпадение (в stext — 11 «печать» и «оттиск»),
// выбор уходит к следующему; «Заменить все…» — предпросмотр (11 строк, фрагменты «было/станет»), «Заменить
// все» в диалоге — отчёт «Заменено: 11», в stext — ни одной «печать», Ctrl+Z отменяет команду одним шагом;
// подписанный документ — «Заменить» серая с причиной.
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QtTest/QtTest>

#include "core/text/stext.h"
#include "services/app_info.h"
#include "services/document_session.h"
#include "support/ui_test.h"
#include "ui/dialogs/replace_dialog.h"
#include "ui/panes/find_bar.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/view/page_view.h"
#include "ui/widgets/search_field.h"
#include "ui/widgets/tool_button.h"

using ot::ui::DocumentView;
using ot::ui::FindBar;
using ot::ui::MainWindow;
using ot::ui::ReplaceDialog;

namespace {

QString fixture(const char* rel) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/") + QLatin1String(rel);
}

DocumentView* openDoc(MainWindow& w, const char* rel) {
    DocumentView* doc = nullptr;
    [&] {
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        w.resize(1440, 900);
        w.show();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QTest::qWaitForWindowActive(&w);
        QVERIFY(w.openFile(fixture(rel)));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        doc = w.currentDocument();
        doc->view().setFocus();
    }();
    return doc;
}

// Сколько раз слово встречается в тексте документа без учёта регистра (stext всех страниц).
int occurrences(DocumentView& doc, const QString& word) {
    int n = 0;
    for (int p = 0; p < doc.session().pageCount(); ++p) {
        const QString text = QString::fromStdString(doc.session().textPage(p).result()->text());
        n += static_cast<int>(text.count(word, Qt::CaseInsensitive));
    }
    return n;
}

}  // namespace

class ReplaceTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-replace"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void replaceOneThenAllWithPreviewAndUndo() {
        std::printf("OT_FEATURE:edit-05\n");
        std::printf("OT_FEATURE:view-18\n");
        MainWindow w;
        DocumentView* doc = openDoc(w, "text/find.pdf");
        QVERIFY(doc != nullptr);
        FindBar& bar = doc->findBar();
        QTest::keyClick(&w, Qt::Key_H, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(bar.isVisible());
        QVERIFY(bar.replaceMode());
        QVERIFY(bar.replaceField()->isVisible());
        QVERIFY(bar.replaceOneButton()->isVisible());
        QVERIFY(bar.field()->hasFocus());

        bar.field()->setText(QStringLiteral("печать"));
        QTest::keyClick(bar.field(), Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!bar.isSearching() && bar.count() == 12, 10000);
        bar.replaceField()->setText(QStringLiteral("оттиск"));
        QVERIFY(bar.replaceOneButton()->isEnabled());
        QTest::mouseClick(bar.replaceOneButton(), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(doc->session().undoState().undoName, QStringLiteral("Replace text"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!bar.isSearching() && !bar.isReplacing() && bar.count() == 11, 10000);
        QCOMPARE(occurrences(*doc, QStringLiteral("оттиск")), 1);
        QCOMPARE(occurrences(*doc, QStringLiteral("печать")), 11);

        // «Заменить все…» — предпросмотр.
        QTest::mouseClick(bar.replaceAllButton(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(bar.replaceDialog() != nullptr, 10000);
        ReplaceDialog* dialog = bar.replaceDialog();
        QCOMPARE(static_cast<int>(dialog->matches().size()), 11);
        QCOMPARE(dialog->replaceable(), 11);
        QCOMPARE(dialog->list()->topLevelItemCount(), 11);
        QVERIFY(dialog->list()->topLevelItem(0)->text(1).contains(QStringLiteral("печать")));
        QVERIFY(dialog->list()->topLevelItem(0)->text(2).contains(QStringLiteral("оттиск")));
        QVERIFY(dialog->summary()->text().contains(QStringLiteral("11")));
        QTest::mouseClick(dialog->replaceButton(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(dialog->hasReport(), 20000);
        QVERIFY2(dialog->summary()->text().startsWith(QStringLiteral("Заменено: 11")),
                 qPrintable(dialog->summary()->text()));
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Replace all"));
        QCOMPARE(occurrences(*doc, QStringLiteral("печать")), 0);
        QCOMPARE(occurrences(*doc, QStringLiteral("оттиск")), 12);
        dialog->close();

        // Одна отмена — вся команда.
        QVERIFY(doc->session().undo().result());
        QCOMPARE(occurrences(*doc, QStringLiteral("печать")), 11);
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Replace text"));
    }

    void signedDocumentBlocksReplace() {
        MainWindow w;
        DocumentView* doc = openDoc(w, "sig/signed-bb.pdf");
        QVERIFY(doc != nullptr);
        FindBar& bar = doc->findBar();
        bar.activate();
        QVERIFY(!bar.replaceButton()->isEnabled());
        QVERIFY(bar.replaceButton()->toolTip().contains(QStringLiteral("подписан")));
        bar.activateReplace();
        QVERIFY(!bar.replaceMode());
    }
};

OT_UI_TEST(ReplaceTest, "replace");
#include "replace_test.moc"
