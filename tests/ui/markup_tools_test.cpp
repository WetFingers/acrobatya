// ui.markup_tools — инструмент «Добавить комментарии» (план 5.1, п. 2; эталон 3.4 «Инструмент»): записка
// щелчком открывает окно, набор пишется в /Contents шагом «Edit note text», инструмент однократный;
// «Выделить» и «Подчеркнуть» протягиванием по тексту latin.pdf дают /QuadPoints по строке; «Вставить текст» —
// /Caret и окно; «Заменить текст» — StrikeOut + Caret группой /IRT /RT /Group одним шагом; быстрые действия
// над выделением «Выделения» (comment-10) создают выделение текста; меню «Отменить: …» называет шаг; документ
// только для чтения — кнопки серые с причиной, щелчок ничего не создаёт.
#include <QAction>
#include <QDir>
#include <QPlainTextEdit>
#include <QSettings>
#include <QtTest/QtTest>

#include "core/annot/annots.h"
#include "core/util/features.h"
#include "services/annot_ops.h"
#include "services/app_info.h"
#include "services/document_session.h"
#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/tools/comment/comment_tool.h"
#include "ui/tools/comment/popup_widget.h"
#include "ui/tools/comment/quick_actions_bar.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/tool_button.h"

using ot::annot::AnnotInfo;
using ot::annot::AnnotProps;
using ot::annot::AnnotType;
using ot::ui::CommentAction;
using ot::ui::CommentMode;
using ot::ui::CommentTool;
using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::PageView;
using ot::ui::PopupLayer;
using ot::ui::ToolRegistry;
using ot::ui::ViewToolId;

namespace {

QString fixture(const char* rel) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/") + QLatin1String(rel);
}

CommentTool& commentTool() {
    return *static_cast<CommentTool*>(ToolRegistry::instance().find(QStringLiteral("comment")));
}

// Окно с latin.pdf (первая строка «The editor builds…» — y 82–90 пт от верха, x от 72), «Добавить
// комментарии» включён, текстовый слой первой страницы загружен.
DocumentView* openComment(MainWindow& w) {
    DocumentView* doc = nullptr;
    [&] {
        w.resize(1440, 900);
        w.show();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(fixture("edit/latin.pdf")));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        doc = w.currentDocument();
        doc->view().setZoomPercent(100);
        doc->view().goToPage(0);
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("comment"), doc));
        doc->view().tools().select().textPage(0);
        QTRY_VERIFY_WITH_TIMEOUT(doc->view().tools().select().hasTextPage(0), 10000);
        doc->view().setFocus();
    }();
    return doc;
}

QPoint at(const PageView& v, double x, double y) { return v.pageTransform(0).map(QPointF(x, y)).toPoint(); }

void drag(PageView& v, QPoint from, QPoint to) {
    QTest::mousePress(v.viewport(), Qt::LeftButton, {}, from);
    QTest::mouseMove(v.viewport(), (from + to) / 2);
    QTest::mouseMove(v.viewport(), to);
    QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, to);
}

std::vector<AnnotInfo> ofType(DocumentView& doc, AnnotType type) {
    std::vector<AnnotInfo> out;
    for (const AnnotInfo& a : ot::annot::list(doc.session(), 0).result()) {
        if (a.type == type) {
            out.push_back(a);
        }
    }
    return out;
}

int countOf(DocumentView& doc, AnnotType type) { return static_cast<int>(ofType(doc, type).size()); }

AnnotProps propsOf(DocumentView& doc, const AnnotInfo& info) {
    return ot::annot::props(doc.session(), info.ref).result().value_or(AnnotProps{});
}

QString undoText(MainWindow& w) {
    for (QAction* a : w.findChildren<QAction*>()) {
        if (a->text().startsWith(QStringLiteral("&Отменить"))) {
            return a->text();
        }
    }
    return {};
}

}  // namespace

class MarkupToolsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-markup_tools"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void cleanup() { ToolRegistry::instance().deactivate(); }

    void panelHasActions() {
        MainWindow w;
        DocumentView* doc = openComment(w);
        QVERIFY(doc != nullptr);
        CommentTool& tool = commentTool();
        QCOMPARE(tool.title(), QStringLiteral("Добавить комментарии"));
        QVERIFY(tool.panel() != nullptr);
        for (const CommentAction a : {CommentAction::Note, CommentAction::Highlight, CommentAction::Underline,
                                      CommentAction::StrikeOut, CommentAction::Squiggly,
                                      CommentAction::InsertText, CommentAction::ReplaceText}) {
            QVERIFY(tool.actionButton(a) != nullptr);
            QVERIFY(tool.actionButton(a)->isEnabled());
        }
        tool.actionButton(CommentAction::Highlight)->click();
        QCOMPARE(doc->view().tools().tool(), ViewToolId::Comment);
        QCOMPARE(CommentTool::modeOf(doc->view()).action(), CommentAction::Highlight);
        QVERIFY(tool.actionButton(CommentAction::Highlight)->isChecked());
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
    }

    void noteCreatesPopupAndTextIsWritten() {
        OT_FEATURE("comment-01");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openComment(w);
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        commentTool().start(CommentAction::Note);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 400, 600));
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Text), 1, 10000);
        // Однократный: после записки — «Выделение».
        QCOMPARE(v.tools().tool(), ViewToolId::Select);
        PopupLayer& popups = PopupLayer::of(v);
        QTRY_COMPARE_WITH_TIMEOUT(popups.count(), 1, 10000);
        ot::ui::PopupWidget* popup = popups.popups().front();
        QTRY_VERIFY(popup->isVisible());
        QVERIFY(!popup->authorLabel()->text().isEmpty());
        const AnnotInfo note = ofType(*doc, AnnotType::Text).front();
        // Значок — 20 × 20 пт с центром в точке щелчка.
        const ot::RectF icon = propsOf(*doc, note).rect;
        QVERIFY2(std::abs((icon.x0 + icon.x1) / 2 - 400) < 1.5, qPrintable(QString::number(icon.x0)));
        popup->editor()->setPlainText(QStringLiteral("Проверить цифры"));
        QTRY_COMPARE_WITH_TIMEOUT(QString::fromStdString(propsOf(*doc, note).contents),
                                  QStringLiteral("Проверить цифры"), 10000);
        QTRY_COMPARE(doc->session().undoState().undoName, QStringLiteral("Edit note text"));
        QTRY_COMPARE(undoText(w), QStringLiteral("&Отменить: текст записки"));
        // Повторный щелчок по записке инструментом «Записка» — то же окно, новой записки нет.
        popups.closeAll();
        commentTool().start(CommentAction::Note);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 400, 600));
        QTRY_COMPARE_WITH_TIMEOUT(popups.count(), 1, 10000);
        QCOMPARE(countOf(*doc, AnnotType::Text), 1);
    }

    void highlightAndUnderlineByDrag() {
        OT_FEATURE("comment-02");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openComment(w);
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        commentTool().start(CommentAction::Highlight);
        drag(v, at(v, 71, 86), at(v, 121, 86));
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Highlight), 1, 10000);
        const AnnotProps hl = propsOf(*doc, ofType(*doc, AnnotType::Highlight).front());
        QCOMPARE(static_cast<int>(hl.quads.size()), 1);
        const ot::RectF q = hl.quads.front().bounds();
        QVERIFY2(q.x0 < 73 && q.x1 > 118 && q.x1 < 126,
                 qPrintable(QStringLiteral("%1 %2").arg(q.x0).arg(q.x1)));
        QVERIFY(q.y0 < 86 && q.y1 > 86);
        // Разметка не однократная: инструмент остаётся.
        QCOMPARE(v.tools().tool(), ViewToolId::Comment);
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Add highlight"));

        commentTool().start(CommentAction::Underline);
        drag(v, at(v, 71, 86), at(v, 121, 86));
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Underline), 1, 10000);
        // Мимо текста «Подчеркнуть» ничего не создаёт, «Выделить» — область.
        drag(v, at(v, 300, 700), at(v, 400, 760));
        QTest::qWait(200);
        QCOMPARE(countOf(*doc, AnnotType::Underline), 1);
        commentTool().start(CommentAction::Highlight);
        drag(v, at(v, 300, 700), at(v, 400, 760));
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Highlight), 2, 10000);
    }

    void insertAndReplaceText() {
        OT_FEATURE("comment-03");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openComment(w);
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        commentTool().start(CommentAction::InsertText);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 91, 86));  // между «The» и «editor»
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Caret), 1, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(PopupLayer::of(v).count(), 1, 10000);
        const AnnotProps caret = propsOf(*doc, ofType(*doc, AnnotType::Caret).front());
        QVERIFY2(caret.rect.x0 < 92 && caret.rect.x1 > 90, qPrintable(QString::number(caret.rect.x0)));
        QCOMPARE(QString::fromStdString(caret.subject), QStringLiteral("Inserted Text"));

        commentTool().start(CommentAction::ReplaceText);
        drag(v, at(v, 94, 86), at(v, 121, 86));  // «editor»
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::StrikeOut), 1, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Caret), 2, 10000);
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Replace text"));
        const AnnotProps strike = propsOf(*doc, ofType(*doc, AnnotType::StrikeOut).front());
        QCOMPARE(QString::fromStdString(strike.replyType), QStringLiteral("Group"));
        QCOMPARE(QString::fromStdString(strike.intent), QStringLiteral("StrikeOutTextEdit"));
        QVERIFY(strike.inReplyTo > 0);
        // Одна отмена снимает обе.
        QVERIFY(doc->session().undo().result());
        QTRY_COMPARE(countOf(*doc, AnnotType::StrikeOut), 0);
        QCOMPARE(countOf(*doc, AnnotType::Caret), 1);
    }

    void quickActionsOnSelection() {
        OT_FEATURE("comment-10");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openComment(w);
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        v.tools().setTool(ViewToolId::Select);
        ot::ui::QuickActionsBar& bar = doc->quickActions();
        QVERIFY(!bar.isVisible());
        drag(v, at(v, 71, 86), at(v, 121, 86));
        QTRY_VERIFY(v.tools().select().hasSelection());
        QTRY_VERIFY(bar.isVisible());
        QVERIFY(bar.button(QStringLiteral("highlight"))->isEnabled());
        bar.button(QStringLiteral("highlight"))->click();
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Highlight), 1, 10000);
        QVERIFY(!v.tools().select().hasSelection());
        QTRY_VERIFY(!bar.isVisible());
        // «Добавить комментарий» — выделение и окно записки.
        drag(v, at(v, 94, 86), at(v, 121, 86));
        QTRY_VERIFY(bar.isVisible());
        bar.run(QStringLiteral("note"));
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Highlight), 2, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(PopupLayer::of(v).count(), 1, 10000);
    }

    void readOnlyLocksActions() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openComment(w);
        QVERIFY(doc != nullptr);
        CommentTool& tool = commentTool();
        doc->setReadOnly(true);
        QTRY_VERIFY(!tool.lockReason().isEmpty());
        QVERIFY(!tool.actionButton(CommentAction::Note)->isEnabled());
        QCOMPARE(tool.actionButton(CommentAction::Note)->toolTip(), tool.lockReason());
        tool.start(CommentAction::Note);
        QVERIFY(doc->view().tools().tool() != ViewToolId::Comment);
        // Быстрые действия: разметка серая.
        PageView& v = doc->view();
        v.tools().setTool(ViewToolId::Select);
        drag(v, at(v, 71, 86), at(v, 121, 86));
        QTRY_VERIFY(doc->quickActions().isVisible());
        QVERIFY(!doc->quickActions().button(QStringLiteral("highlight"))->isEnabled());
        doc->setReadOnly(false);
        QTRY_VERIFY(tool.lockReason().isEmpty());
    }
};

OT_UI_TEST(MarkupToolsTest, "markup_tools");
#include "markup_tools_test.moc"
