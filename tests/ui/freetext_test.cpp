// ui.freetext — текстовое поле и выноска (план 5.2, п. 1; эталон 3.4 «Инструмент»: Текст (J), Выноска;
// comment-04, comment-05, comment-09): щелчок «Текстовым полем» открывает редактор на странице, набор и Esc
// создают FreeText одним шагом «Add text box» со шрифтом /DA из стиля, пустой набор — ничего; щелчок по
// своему полю и двойной щелчок «Выбором объектов» — правка текста одним шагом «Edit text box», пустой текст
// удаляет поле; выноска протягиванием — /IT /FreeTextCallout, /CL из трёх точек с концом стрелки в точке
// нажатия; «Формат текста» меняет шрифт, кегль, цвет и выравнивание открытого редактора, выбранного поля и
// стиль следующего; клавиша J — «Текстовое поле».
#include <QApplication>
#include <QDir>
#include <QKeyEvent>
#include <QSettings>
#include <QtTest/QtTest>

#include <cmath>

#include "core/annot/annots.h"
#include "core/annot/defaults.h"
#include "core/util/features.h"
#include "services/annot_ops.h"
#include "services/app_info.h"
#include "services/comment_settings.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/tools/comment/comment_tool.h"
#include "ui/tools/comment/inline_text_editor.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

using ot::annot::AnnotInfo;
using ot::annot::AnnotProps;
using ot::annot::AnnotType;
using ot::ui::CommentAction;
using ot::ui::CommentTool;
using ot::ui::DocumentView;
using ot::ui::InlineTextEditor;
using ot::ui::MainWindow;
using ot::ui::PageView;
using ot::ui::ToolRegistry;
using ot::ui::ViewToolId;

namespace {

QString fixture(const char* rel) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/") + QLatin1String(rel);
}

CommentTool& commentTool() {
    return *static_cast<CommentTool*>(ToolRegistry::instance().find(QStringLiteral("comment")));
}

DocumentView* openDoc(MainWindow& w, const char* rel) {
    DocumentView* doc = nullptr;
    [&] {
        w.resize(1440, 900);
        w.show();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(fixture(rel)));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        doc = w.currentDocument();
        doc->view().setZoomPercent(100);
        doc->view().goToPage(0);
        doc->view().setFocus();
    }();
    return doc;
}

QPoint at(const PageView& v, double x, double y, int page = 0) {
    return v.pageTransform(page).map(QPointF(x, y)).toPoint();
}

std::vector<AnnotInfo> ofType(DocumentView& doc, AnnotType type, int page = 0) {
    std::vector<AnnotInfo> out;
    for (const AnnotInfo& a : ot::annot::list(doc.session(), page).result()) {
        if (a.type == type) {
            out.push_back(a);
        }
    }
    return out;
}

int countOf(DocumentView& doc, AnnotType type) { return static_cast<int>(ofType(doc, type).size()); }

AnnotProps propsOf(DocumentView& doc, const ot::annot::AnnotRef& ref) {
    return ot::annot::props(doc.session(), ref).result().value_or(AnnotProps{});
}

InlineTextEditor* editorOf(const PageView& v) { return InlineTextEditor::active(v); }

// Набор: QTest::keyClicks не умеет кириллицу (asciiToKey) — события клавиш с текстом.
void typeText(InlineTextEditor* editor, const QString& text) {
    for (const QChar ch : text) {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(ch));
        QApplication::sendEvent(editor, &press);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, QString(ch));
        QApplication::sendEvent(editor, &release);
    }
}

}  // namespace

class FreeTextTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-freetext"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void cleanup() { ToolRegistry::instance().deactivate(); }

    void clickTypeEscCreatesTextBox() {
        OT_FEATURE("comment-04");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("comment"), doc));
        commentTool().start(CommentAction::TextBox);
        QVERIFY(commentTool().actionButton(CommentAction::TextBox)->isChecked());

        // Пустой набор — ничего.
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 300, 500));
        QTRY_VERIFY(editorOf(v) != nullptr);
        QTest::keyClick(editorOf(v), Qt::Key_Escape);
        QTRY_VERIFY(editorOf(v) == nullptr);
        QTest::qWait(100);
        QCOMPARE(countOf(*doc, AnnotType::FreeText), 0);

        commentTool().start(CommentAction::TextBox);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 300, 500));
        QTRY_VERIFY(editorOf(v) != nullptr);
        InlineTextEditor* editor = editorOf(v);
        QVERIFY(editor->isVisible());
        QVERIFY(std::abs(editor->props().rect.x0 - 300) < 1 && std::abs(editor->props().rect.y0 - 500) < 1);
        typeText(editor, QStringLiteral("Привет, мир"));
        QTest::keyClick(editor, Qt::Key_Escape);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::FreeText), 1, 10000);
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Add text box"));
        const AnnotInfo box = ofType(*doc, AnnotType::FreeText).front();
        const AnnotProps p = propsOf(*doc, box.ref);
        QCOMPARE(QString::fromStdString(p.contents), QStringLiteral("Привет, мир"));
        QCOMPARE(QString::fromStdString(p.fontName), QStringLiteral("Arial"));
        QCOMPARE(p.fontSize, 12.0f);
        QVERIFY(std::abs(p.rect.x0 - 300) < 1);
        // Однократный: после поля — прежний инструмент.
        QTRY_VERIFY(v.tools().tool() != ViewToolId::Comment);

        // Щелчок «Текстовым полем» по своему полю — его правка; пустой текст удаляет поле.
        commentTool().start(CommentAction::TextBox);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {},
                          at(v, p.rect.x0 + 10, (p.rect.y0 + p.rect.y1) / 2));
        QTRY_VERIFY(editorOf(v) != nullptr && editorOf(v)->ref().has_value());
        QCOMPARE(editorOf(v)->toPlainText(), QStringLiteral("Привет, мир"));
        editorOf(v)->selectAll();
        QTest::keyClick(editorOf(v), Qt::Key_Delete);
        QTest::keyClick(editorOf(v), Qt::Key_Escape);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::FreeText), 0, 10000);
        doc->session().undo();
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::FreeText), 1, 10000);
    }

    void doubleClickEditsText() {
        OT_FEATURE("comment-09");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        AnnotProps p = ot::annot::toolDefaults(AnnotType::FreeText);
        p.rect = {200, 400, 380, 430};
        p.contents = "Первый";
        const auto ref = ot::annot::create(doc->session(), 0, p).result();
        v.tools().setTool(ViewToolId::AnnotSelect);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 250, 415));
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(v.tools().annotSelect().selection().size()), 1, 10000);
        QTest::mouseDClick(v.viewport(), Qt::LeftButton, {}, at(v, 250, 415));
        QTRY_VERIFY_WITH_TIMEOUT(editorOf(v) != nullptr, 10000);
        QCOMPARE(*editorOf(v)->ref(), ref);
        editorOf(v)->moveCursor(QTextCursor::End);
        typeText(editorOf(v), QStringLiteral(" и второй"));
        // Щелчок мимо — фиксация.
        v.setFocus(Qt::MouseFocusReason);
        QTRY_COMPARE_WITH_TIMEOUT(QString::fromStdString(propsOf(*doc, ref).contents),
                                  QStringLiteral("Первый и второй"), 10000);
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Edit text box"));
    }

    void calloutByDrag() {
        OT_FEATURE("comment-05");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("comment"), doc));
        commentTool().start(CommentAction::Callout);
        const QPoint tip = at(v, 120, 600);
        const QPoint box = at(v, 260, 520);
        QTest::mousePress(v.viewport(), Qt::LeftButton, {}, tip);
        QTest::mouseMove(v.viewport(), (tip + box) / 2);
        QTest::mouseMove(v.viewport(), box);
        QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, box);
        QTRY_VERIFY(editorOf(v) != nullptr);
        typeText(editorOf(v), QStringLiteral("Выноска"));
        QTest::keyClick(editorOf(v), Qt::Key_Escape);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::FreeText), 1, 10000);
        const AnnotProps p = propsOf(*doc, ofType(*doc, AnnotType::FreeText).front().ref);
        QCOMPARE(QString::fromStdString(p.intent), QStringLiteral("FreeTextCallout"));
        QCOMPARE(static_cast<int>(p.callout.size()), 3);
        QVERIFY2(std::abs(p.callout[0].x - 120) < 2 && std::abs(p.callout[0].y - 600) < 2,
                 qPrintable(QStringLiteral("%1 %2").arg(p.callout[0].x).arg(p.callout[0].y)));
        QCOMPARE(p.lineStartStyle, ot::annot::LineEnding::OpenArrow);
        QVERIFY(std::abs(p.rect.x0 - 260) < 2);
        QVERIFY(p.rect.y0 < 520 && p.rect.y1 > 520);
        QCOMPARE(p.borderWidth, 1.0f);
    }

    void formatPanelEditorSelectionAndStyle() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("comment"), doc));
        CommentTool& tool = commentTool();
        QVERIFY(tool.panel() != nullptr);

        // Стиль следующего поля: Times New Roman 16, по центру.
        tool.start(CommentAction::TextBox);
        QVERIFY(tool.fontBox()->isEnabled());
        tool.fontBox()->setCurrentIndex(tool.fontBox()->findData(QStringLiteral("Times New Roman")));
        tool.fontSizeField()->setValue(16);
        tool.alignBar()->setCurrentIndex(1);
        const auto style = ot::comments::toolStyle(ot::Settings(), QStringLiteral("textBox"));
        QVERIFY(style.font.has_value() && *style.font == "Times New Roman");
        QVERIFY(style.fontSize.has_value() && *style.fontSize == 16.0f);
        QVERIFY(style.align.has_value() && *style.align == 1);

        // Открытый редактор: кегль меняется на ходу, в документ — при фиксации.
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 100, 650));
        QTRY_VERIFY(editorOf(v) != nullptr);
        QCOMPARE(QString::fromStdString(editorOf(v)->props().fontName), QStringLiteral("Times New Roman"));
        typeText(editorOf(v), QStringLiteral("Формат"));
        tool.fontSizeField()->setValue(20);
        QVERIFY(editorOf(v) != nullptr);
        QCOMPARE(editorOf(v)->props().fontSize, 20.0f);
        QTest::keyClick(editorOf(v), Qt::Key_Escape);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::FreeText), 1, 10000);
        const auto ref = ofType(*doc, AnnotType::FreeText).front().ref;
        AnnotProps p = propsOf(*doc, ref);
        QCOMPARE(QString::fromStdString(p.fontName), QStringLiteral("Times New Roman"));
        QCOMPARE(p.fontSize, 20.0f);
        QCOMPARE(p.align, 1);

        // Выбранное поле: цвет текста и шрифт — шагом «Set annotation properties».
        v.tools().setTool(ViewToolId::AnnotSelect);
        v.tools().annotSelect().setSelection(ofType(*doc, AnnotType::FreeText));
        QTRY_COMPARE(QString(tool.fontBox()->currentData().toString()), QStringLiteral("Times New Roman"));
        tool.fontBox()->setCurrentIndex(tool.fontBox()->findData(QStringLiteral("Courier New")));
        QTRY_COMPARE_WITH_TIMEOUT(QString::fromStdString(propsOf(*doc, ref).fontName),
                                  QStringLiteral("Courier New"), 10000);
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Set annotation properties"));
        // Стиль следующего поля не тронут.
        QCOMPARE(
            QString::fromStdString(*ot::comments::toolStyle(ot::Settings(), QStringLiteral("textBox")).font),
            QStringLiteral("Times New Roman"));
    }
};

OT_UI_TEST(FreeTextTest, "freetext");
#include "freetext_test.moc"
