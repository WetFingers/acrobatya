// ui.comments_exchange — FDF/XFDF и сводка из панели «Комментарии» (план 5.4, п. 4; эталон 3.4, 7.3;
// comment-25, comment-27, security-36): «… → Экспорт всех → XFDF» пишет файл; после удаления комментариев
// «Импорт» спрашивает с именем файла и числом объектов, отказ — ничего не меняет, согласие — одна правка
// «Import comments» (undo убирает); файл другого документа — отказ без вопроса; «Сводка комментариев…» —
// новая вкладка «Без имени» без истории, исходный документ не тронут.
#include <QDir>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "core/annot/annots.h"
#include "core/annot/defaults.h"
#include "core/util/features.h"
#include "services/annot_ops.h"
#include "services/app_info.h"
#include "services/comments_ops.h"
#include "services/document_session.h"
#include "support/ui_test.h"
#include "ui/dialogs/summary_dialog.h"
#include "ui/panes/comments_pane.h"
#include "ui/shell/document_tabs.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/nav_pane.h"
#include "ui/view/page_view.h"

using ot::annot::AnnotProps;
using ot::annot::AnnotRef;
using ot::annot::AnnotType;
using ot::ui::CommentsPane;
using ot::ui::DocumentView;
using ot::ui::MainWindow;

namespace {

DocumentView* open(MainWindow& w) {
    DocumentView* doc = nullptr;
    [&] {
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        w.resize(1440, 900);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/demo/report.pdf")));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        doc = w.currentDocument();
    }();
    return doc;
}

AnnotRef note(DocumentView& doc, int page, const char* text) {
    AnnotProps p = ot::annot::toolDefaults(AnnotType::Text);
    p.rect = {80, 120, 100, 140};
    p.author = "Анна";
    p.contents = text;
    return ot::annot::create(doc.session(), page, p).result();
}

int commentCount(DocumentView& doc) {
    int n = 0;
    for (int p = 0; p < doc.session().pageCount(); ++p) {
        for (const ot::annot::AnnotInfo& a : ot::annot::list(doc.session(), p).result()) {
            n += ot::annot::isComment(a.type) ? 1 : 0;
        }
    }
    return n;
}

}  // namespace

class CommentsExchangeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-comments_exchange"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
        qRegisterMetaType<ot::ui::DocumentView*>();
        qRegisterMetaType<ot::annot::ImportReport>();
    }

    void exportImportWithConfirmation() {
        OT_FEATURE("comment-27");
        OT_FEATURE("security-36");
        MainWindow w;
        DocumentView* doc = open(w);
        QVERIFY(doc != nullptr);
        w.navPane().showPane(QStringLiteral("comments"));
        auto* pane = qobject_cast<CommentsPane*>(w.navPane().page(QStringLiteral("comments")));
        const AnnotRef a = note(*doc, 0, "первая");
        note(*doc, 1, "вторая");
        ot::comments::reply(doc->session(), a, {.author = "Борис", .contents = "ответ"}).waitForFinished();
        QTRY_COMPARE_WITH_TIMEOUT(pane->tree().count(), 3, 10000);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("report.xfdf"));
        pane->setSaveChooser([&](const QString& suggested, const QString& filter) {
            [&] {
                QVERIFY(suggested.endsWith(QStringLiteral("report.xfdf")));
                QVERIFY(filter.contains(QStringLiteral("xfdf")));
            }();
            return path;
        });
        QSignalSpy exported(pane, &CommentsPane::exported);
        pane->fillMoreMenu();
        auto* exportXfdf = pane->moreMenu()->findChild<QAction*>(QStringLiteral("commentsExportXfdf"));
        QVERIFY(exportXfdf != nullptr);
        exportXfdf->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(exported.count(), 1, 10000);
        QVERIFY(QFile::exists(path));

        // Удалить всё и вернуть импортом.
        for (const ot::annot::Comment& c : pane->tree().roots) {
            ot::comments::removeBranch(doc->session(), c.ref).waitForFinished();
        }
        QTRY_COMPARE_WITH_TIMEOUT(commentCount(*doc), 0, 10000);
        QString askedFile;
        int askedCount = -1;
        bool answer = false;
        pane->setImportConfirm([&](const QString& file, int count) {
            askedFile = file;
            askedCount = count;
            return answer;
        });
        pane->setOpenChooser([&](const QString&) { return path; });
        pane->importComments();
        QCOMPARE(askedFile, QStringLiteral("report.xfdf"));
        QCOMPARE(askedCount, 3);
        QTest::qWait(100);
        QCOMPARE(commentCount(*doc), 0);  // «Нет» — ничего
        answer = true;
        QSignalSpy imported(pane, &CommentsPane::imported);
        pane->importComments();
        QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 1, 10000);
        const auto report = imported.front().front().value<ot::annot::ImportReport>();
        QCOMPARE(report.created, 3);
        QCOMPARE(commentCount(*doc), 3);
        QCOMPARE(doc->session().undoState().undoName, QString::fromLatin1(ot::comments::kImportStep));
        doc->session().undo().waitForFinished();
        QTRY_COMPARE_WITH_TIMEOUT(commentCount(*doc), 0, 10000);

        // Файл другого документа — отказ без вопроса.
        QFile foreign(dir.filePath(QStringLiteral("other.xfdf")));
        QVERIFY(foreign.open(QIODevice::WriteOnly));
        foreign.write(
            "<?xml version=\"1.0\"?><xfdf xmlns=\"http://ns.adobe.com/xfdf/\"><annots>"
            "<square page=\"0\" rect=\"10,10,50,50\" name=\"x\"/></annots><f href=\"Договор.pdf\"/></xfdf>");
        foreign.close();
        askedFile.clear();
        QSignalSpy refused(pane, &CommentsPane::importRefused);
        pane->importFrom(foreign.fileName());
        QCOMPARE(refused.count(), 1);
        QVERIFY(askedFile.isEmpty());
        QCOMPARE(commentCount(*doc), 0);
    }

    void summaryOpensUntitledTab() {
        OT_FEATURE("comment-25");
        MainWindow w;
        DocumentView* doc = open(w);
        QVERIFY(doc != nullptr);
        w.navPane().showPane(QStringLiteral("comments"));
        auto* pane = qobject_cast<CommentsPane*>(w.navPane().page(QStringLiteral("comments")));
        note(*doc, 0, "для сводки");
        QTRY_COMPARE_WITH_TIMEOUT(pane->tree().count(), 1, 10000);
        const int tabs = w.tabs().count();
        auto* dialog = new ot::ui::SummaryDialog(w, *doc, pane, &w);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        QVERIFY(dialog->layoutButton(ot::annot::SummaryLayout::DocumentAndComments)->isChecked());
        dialog->layoutButton(ot::annot::SummaryLayout::CommentsOnly)->setChecked(true);
        QCOMPARE(dialog->options().layout, ot::annot::SummaryLayout::CommentsOnly);
        QSignalSpy created(dialog, &ot::ui::SummaryDialog::created);
        dialog->createButton()->click();
        QTRY_COMPARE_WITH_TIMEOUT(created.count(), 1, 15000);
        auto* result = created.front().front().value<DocumentView*>();
        QVERIFY(result != nullptr);
        QCOMPARE(w.tabs().count(), tabs + 1);
        QCOMPARE(result->title(), QStringLiteral("Без имени"));
        QVERIFY(result->path().isEmpty());
        QVERIFY(!result->session().undoState().canUndo);
        QVERIFY(result->session().pageCount() >= 1);
        QVERIFY(!doc->session().undoState().undoName.contains(QStringLiteral("summary")));
    }
};

OT_UI_TEST(CommentsExchangeTest, "comments_exchange");
#include "comments_exchange_test.moc"
