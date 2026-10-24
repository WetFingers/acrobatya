// ui.ink_select — карандаш, ластик и «Выбор объектов» с буфером (план 5.1, п. 3; эталон 3.4 «Инструмент»,
// «Выделение и правка на странице»; comment-06, comment-43): штрих мышью — Ink с точками после
// Дугласа–Пекера, инструмент остаётся; ластик поперёк штриха делит его на два; выбранный штрих тянется вместе
// с вершинами, разметка текста не двигается; Ctrl+C / Ctrl+V копируют записку на другую страницу и в другой
// документ (исходные координаты, новый /NM), Ctrl+X вырезает; контекстное меню — Копировать, Вырезать,
// Удалить, Заблокировать (заблокированное не двигается и не удаляется), «Открыть окно записки», «Сделать
// свойствами по умолчанию» (стиль инструмента в settings.ini).
#include <QAction>
#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QMenu>
#include <QMimeData>
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
#include "ui/tools/comment/popup_widget.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

using ot::annot::AnnotInfo;
using ot::annot::AnnotProps;
using ot::annot::AnnotType;
using ot::ui::AnnotSelectTool;
using ot::ui::CommentAction;
using ot::ui::CommentTool;
using ot::ui::DocumentView;
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
        const int before = w.documentCount();
        QVERIFY(w.openFile(fixture(rel)));
        QTRY_VERIFY(w.documentCount() == before + 1 && w.currentDocument() != nullptr &&
                    w.currentDocument()->view().pageCount() > 0);
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

int countOf(DocumentView& doc, AnnotType type, int page = 0) {
    return static_cast<int>(ofType(doc, type, page).size());
}

AnnotProps propsOf(DocumentView& doc, const ot::annot::AnnotRef& ref) {
    return ot::annot::props(doc.session(), ref).result().value_or(AnnotProps{});
}

// Штрих мышью по точкам страницы.
void stroke(PageView& v, const std::vector<QPointF>& points) {
    QTest::mousePress(v.viewport(), Qt::LeftButton, {}, at(v, points.front().x(), points.front().y()));
    for (std::size_t i = 1; i < points.size(); ++i) {
        QTest::mouseMove(v.viewport(), at(v, points[i].x(), points[i].y()));
    }
    QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, at(v, points.back().x(), points.back().y()));
}

std::vector<QPointF> horizontal(double x0, double x1, double y) {
    std::vector<QPointF> out;
    for (double x = x0; x <= x1 + 0.01; x += 10) {
        out.emplace_back(x, y);
    }
    return out;
}

ot::annot::AnnotRef createNote(DocumentView& doc, int page, const char* text) {
    AnnotProps p = ot::annot::toolDefaults(AnnotType::Text);
    p.rect = {100, 100, 120, 120};
    p.contents = text;
    return ot::annot::create(doc.session(), page, p).result();
}

}  // namespace

class InkSelectTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-ink_select"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void cleanup() { ToolRegistry::instance().deactivate(); }

    void inkStrokeAndEraser() {
        OT_FEATURE("comment-06");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("comment"), doc));
        commentTool().start(CommentAction::Ink);
        stroke(v, horizontal(300, 500, 500));
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Ink), 1, 10000);
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Add ink"));
        QCOMPARE(v.tools().tool(), ViewToolId::Comment);  // карандаш остаётся
        const AnnotInfo ink = ofType(*doc, AnnotType::Ink).front();
        const AnnotProps drawn = propsOf(*doc, ink.ref);
        QCOMPARE(static_cast<int>(drawn.ink.size()), 1);
        // 21 точка мыши на прямой → после Дугласа–Пекера (ε = 0,5 пт) — концы.
        QVERIFY2(drawn.ink[0].size() <= 3, qPrintable(QString::number(drawn.ink[0].size())));

        commentTool().start(CommentAction::Eraser);
        QCOMPARE(v.tools().tool(), ViewToolId::Comment);
        stroke(v, {{400, 485}, {400, 495}, {400, 505}, {400, 515}});
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(propsOf(*doc, ink.ref).ink.size()), 2, 10000);
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Erase ink"));
        const AnnotProps split = propsOf(*doc, ink.ref);
        QVERIFY(split.ink[0].back().x < 400 && split.ink[1].front().x > 400);
    }

    void selectMovesInkNotMarkup() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        AnnotProps inkProps = ot::annot::toolDefaults(AnnotType::Ink);
        inkProps.ink = {{{300, 600}, {400, 600}}};
        const auto ink = ot::annot::create(doc->session(), 0, inkProps).result();
        AnnotProps hlProps = ot::annot::toolDefaults(AnnotType::Highlight);
        hlProps.quads = {{{72, 82}, {120, 82}, {72, 90}, {120, 90}}};
        const auto hl = ot::annot::create(doc->session(), 0, hlProps).result();
        v.tools().setTool(ViewToolId::AnnotSelect);
        AnnotSelectTool& tool = v.tools().annotSelect();

        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 350, 600));
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(tool.selection().size()), 1, 10000);
        QCOMPARE(tool.selection().front().ref, ink);
        const QPoint from = at(v, 350, 600);
        QTest::mousePress(v.viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(v.viewport(), from + QPoint(20, 10));
        QTest::mouseMove(v.viewport(), from + QPoint(40, 20));
        QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, from + QPoint(40, 20));
        QTRY_VERIFY_WITH_TIMEOUT(propsOf(*doc, ink).ink[0][0].x > 301, 10000);
        const double dx = propsOf(*doc, ink).ink[0][0].x - 300;
        QVERIFY2(std::abs(dx - 40 / v.zoom()) < 1.0, qPrintable(QString::number(dx)));
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Set annotation rectangle"));

        // Разметка: выбирается, но не тянется и без ручек.
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 96, 86));
        QTRY_VERIFY_WITH_TIMEOUT(tool.selection().size() == 1 && tool.selection().front().ref == hl, 10000);
        QVERIFY(!AnnotSelectTool::movable(tool.selection().front()));
        QVERIFY(!tool.handleAt(tool.frames().front().bottomRight()).has_value());
        const quint64 revision = doc->session().revision();
        const QPoint hfrom = at(v, 96, 86);
        QTest::mousePress(v.viewport(), Qt::LeftButton, {}, hfrom);
        QTest::mouseMove(v.viewport(), hfrom + QPoint(30, 30));
        QTest::mouseMove(v.viewport(), hfrom + QPoint(60, 60));
        QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, hfrom + QPoint(60, 60));
        QTest::qWait(200);
        QCOMPARE(doc->session().revision(), revision);
        QCOMPARE(propsOf(*doc, hl).quads.front().ul.x, 72.0f);
    }

    void clipboardBetweenPagesAndDocuments() {
        OT_FEATURE("comment-43");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "text/find.pdf");
        QVERIFY(doc != nullptr);
        QVERIFY(doc->view().pageCount() >= 2);
        PageView& v = doc->view();
        const auto note = createNote(*doc, 0, "copy me");
        const AnnotProps source = propsOf(*doc, note);
        v.tools().setTool(ViewToolId::AnnotSelect);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 110, 110));
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(v.tools().annotSelect().selection().size()), 1, 10000);
        QTest::keyClick(v.viewport(), Qt::Key_C, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(v.tools().annotSelect().canPaste(), 10000);

        // Другая страница: исходные координаты.
        v.goToPage(1);
        QTRY_COMPARE(v.currentPage(), 1);
        QTest::keyClick(v.viewport(), Qt::Key_V, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Text, 1), 1, 10000);
        const AnnotInfo copy = ofType(*doc, AnnotType::Text, 1).front();
        const AnnotProps pasted = propsOf(*doc, copy.ref);
        QCOMPARE(QString::fromStdString(pasted.contents), QStringLiteral("copy me"));
        QVERIFY(pasted.nm != source.nm);
        QVERIFY(std::abs(pasted.rect.x0 - source.rect.x0) < 0.1 &&
                std::abs(pasted.rect.y0 - source.rect.y0) < 0.1);
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Paste annotations"));
        // Вставленная — выбрана.
        QTRY_VERIFY(v.tools().annotSelect().selection().size() == 1 &&
                    v.tools().annotSelect().selection().front().ref == copy.ref);

        // Другой документ.
        DocumentView* other = openDoc(w, "edit/latin.pdf");
        QVERIFY(other != nullptr && other != doc);
        other->view().tools().setTool(ViewToolId::AnnotSelect);
        other->view().tools().annotSelect().paste();
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*other, AnnotType::Text), 1, 10000);
        QCOMPARE(
            QString::fromStdString(propsOf(*other, ofType(*other, AnnotType::Text).front().ref).contents),
            QStringLiteral("copy me"));

        // Вырезать: копия в буфере, исходной нет.
        QVERIFY(w.closeDocument(1));
        QTRY_VERIFY(w.currentDocument() == doc);
        v.tools().annotSelect().setSelection({copy});
        v.tools().annotSelect().cutSelection();
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Text, 1), 0, 10000);
        QVERIFY(v.tools().annotSelect().canPaste());
    }

    void contextMenuLockAndDefaults() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        AnnotProps p = ot::annot::toolDefaults(AnnotType::Text);
        p.rect = {200, 400, 220, 420};
        p.color = ot::annot::AnnotColor::fromRgb(0x2196F3);
        p.icon = "Key";
        const auto note = ot::annot::create(doc->session(), 0, p).result();
        v.tools().setTool(ViewToolId::AnnotSelect);
        AnnotSelectTool& tool = v.tools().annotSelect();
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 210, 410));
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(tool.selection().size()), 1, 10000);

        QMenu menu;
        tool.fillContextMenu(menu);
        auto action = [&menu](const char* name) { return menu.findChild<QAction*>(QLatin1String(name)); };
        for (const char* name : {"annotCopy", "annotCut", "annotDelete", "annotLock", "commentOpenPopup",
                                 "commentMakeDefault"}) {
            QVERIFY2(action(name) != nullptr, name);
        }
        QVERIFY(action("commentOpenPopup")->isEnabled());
        action("commentOpenPopup")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(ot::ui::PopupLayer::of(v).count(), 1, 10000);

        // «Сделать свойствами по умолчанию»: цвет и иконка — стиль записки.
        action("commentMakeDefault")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(
            ot::comments::toolStyle(ot::Settings(), QStringLiteral("note")).icon.has_value(), 10000);
        const ot::comments::ToolStyle style = ot::comments::toolStyle(ot::Settings(), QStringLiteral("note"));
        QCOMPARE(QString::fromStdString(*style.icon), QStringLiteral("Key"));
        QCOMPARE(style.color->toRgb(), 0x2196F3u);
        QCOMPARE(QString::fromStdString(CommentTool::modeOf(v).newProps(CommentAction::Note).icon),
                 QStringLiteral("Key"));

        // Заблокировать: не удаляется.
        action("annotLock")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(propsOf(*doc, note).flags & ot::annot::kAnnotLocked, 10000);
        QTRY_VERIFY(!tool.selection().empty() && tool.selection().front().locked());
        tool.deleteSelection();
        QTest::qWait(200);
        QCOMPARE(countOf(*doc, AnnotType::Text), 1);
        QMenu again;
        tool.fillContextMenu(again);
        QVERIFY(again.findChild<QAction*>(QStringLiteral("annotLock"))->isChecked());
        QVERIFY(!again.findChild<QAction*>(QStringLiteral("annotDelete"))->isEnabled());
        again.findChild<QAction*>(QStringLiteral("annotLock"))->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!(propsOf(*doc, note).flags & ot::annot::kAnnotLocked), 10000);
        ot::Settings settings;
        settings.reset(ot::settings::kCommentToolStyles.name);
    }
};

OT_UI_TEST(InkSelectTest, "ink_select");
#include "ink_select_test.moc"
