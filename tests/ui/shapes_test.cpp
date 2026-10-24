// ui.shapes — фигуры (план 5.2, п. 2; эталон 3.4 «Инструмент»: Фигуры — прямоугольник (R), эллипс, линия,
// стрелка, многоугольник, ломаная, облако; comment-07): протягивание — прямоугольник и овал (Shift —
// квадрат), линия (Shift — 45°) и стрелка (/LE открытая, /IT /LineArrow); щелчками — многоугольник (двойной
// щелчок — конец), облако (Enter — конец, /BE, /IT /PolygonCloud), ломаная (Backspace убирает вершину); стиль
// панели — заливка и концы линии; маркеры вершин «Выбора объектов» — конец линии, вершина многоугольника и
// колено выноски тянутся шагом «Move vertex» (Shift — 45°), undo возвращает.
#include <QDir>
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
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

using ot::annot::AnnotInfo;
using ot::annot::AnnotProps;
using ot::annot::AnnotType;
using ot::annot::LineEnding;
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

void drag(PageView& v, QPointF from, QPointF to, Qt::KeyboardModifiers mods = {}) {
    const QPoint a = at(v, from.x(), from.y());
    const QPoint b = at(v, to.x(), to.y());
    QTest::mousePress(v.viewport(), Qt::LeftButton, mods, a);
    QTest::mouseMove(v.viewport(), (a + b) / 2);
    QTest::mouseMove(v.viewport(), b);
    QTest::mouseRelease(v.viewport(), Qt::LeftButton, mods, b);
}

bool near(float a, double b, double eps = 1.5) { return std::abs(a - b) < eps; }

}  // namespace

class ShapesTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-shapes"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void cleanup() { ToolRegistry::instance().deactivate(); }

    void dragShapesAndShift() {
        OT_FEATURE("comment-07");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("comment"), doc));

        commentTool().start(CommentAction::Rectangle);
        drag(v, {100, 300}, {200, 350});
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Square), 1, 10000);
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Add rectangle"));
        QTRY_VERIFY(v.tools().tool() != ViewToolId::Comment);  // однократный
        AnnotProps r = propsOf(*doc, ofType(*doc, AnnotType::Square).front().ref);
        QVERIFY(near(r.rect.width(), 100, 3) && near(r.rect.height(), 50, 3));

        // Shift — квадрат.
        commentTool().start(CommentAction::Oval);
        drag(v, {300, 300}, {400, 340}, Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Circle), 1, 10000);
        r = propsOf(*doc, ofType(*doc, AnnotType::Circle).front().ref);
        QVERIFY2(near(r.rect.width(), r.rect.height(), 3),
                 qPrintable(QStringLiteral("%1 × %2").arg(r.rect.width()).arg(r.rect.height())));

        // Линия с Shift — под 45°.
        commentTool().start(CommentAction::Line);
        drag(v, {100, 450}, {200, 480}, Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Line), 1, 10000);
        const AnnotProps line = propsOf(*doc, ofType(*doc, AnnotType::Line).front().ref);
        QVERIFY2(near(line.lineEnd.y, line.lineStart.y, 1.5),
                 qPrintable(QStringLiteral("%1 → %2").arg(line.lineStart.y).arg(line.lineEnd.y)));
        QCOMPARE(line.lineEndStyle, LineEnding::None);

        // Стрелка — открытая стрелка на конце, /IT /LineArrow.
        commentTool().start(CommentAction::Arrow);
        drag(v, {300, 450}, {420, 520});
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Line), 2, 10000);
        AnnotProps arrow;
        for (const AnnotInfo& a : ofType(*doc, AnnotType::Line)) {
            const AnnotProps p = propsOf(*doc, a.ref);
            if (p.intent == "LineArrow") {
                arrow = p;
            }
        }
        QCOMPARE(arrow.lineEndStyle, LineEnding::OpenArrow);
        QVERIFY(near(arrow.lineEnd.x, 420, 2) && near(arrow.lineEnd.y, 520, 2));

        // Щелчок на месте — ничего.
        commentTool().start(CommentAction::Rectangle);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 500, 600));
        QTest::qWait(150);
        QCOMPARE(countOf(*doc, AnnotType::Square), 1);
    }

    void polygonCloudPolyline() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("comment"), doc));

        // Многоугольник: две вершины щелчками, третья — двойным щелчком (конец).
        commentTool().start(CommentAction::Polygon);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 100, 300));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 200, 300));
        QCOMPARE(static_cast<int>(CommentTool::modeOf(v).polyVertices().size()), 2);
        QTest::mouseDClick(v.viewport(), Qt::LeftButton, {}, at(v, 150, 380));
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Polygon), 1, 10000);
        const AnnotProps poly = propsOf(*doc, ofType(*doc, AnnotType::Polygon).front().ref);
        QCOMPARE(static_cast<int>(poly.vertices.size()), 3);
        QVERIFY(!poly.cloudy);
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Add polygon"));

        // Облако: Enter — конец.
        commentTool().start(CommentAction::Cloud);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 300, 300));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 420, 300));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 420, 400));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 300, 400));
        QTest::keyClick(v.viewport(), Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Polygon), 2, 10000);
        AnnotProps cloud;
        for (const AnnotInfo& a : ofType(*doc, AnnotType::Polygon)) {
            const AnnotProps p = propsOf(*doc, a.ref);
            if (p.cloudy) {
                cloud = p;
            }
        }
        QCOMPARE(static_cast<int>(cloud.vertices.size()), 4);
        QCOMPARE(QString::fromStdString(cloud.intent), QStringLiteral("PolygonCloud"));

        // Ломаная: Backspace убирает вершину, Enter — конец; Esc — отменяет.
        commentTool().start(CommentAction::PolyLine);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 100, 500));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 160, 560));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 220, 500));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 260, 600));
        QTest::keyClick(v.viewport(), Qt::Key_Backspace);
        QCOMPARE(static_cast<int>(CommentTool::modeOf(v).polyVertices().size()), 3);
        QTest::keyClick(v.viewport(), Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::PolyLine), 1, 10000);
        QCOMPARE(
            static_cast<int>(propsOf(*doc, ofType(*doc, AnnotType::PolyLine).front().ref).vertices.size()),
            3);
        commentTool().start(CommentAction::PolyLine);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 100, 650));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 200, 650));
        QTest::keyClick(v.viewport(), Qt::Key_Escape);
        QVERIFY(CommentTool::modeOf(v).polyVertices().empty());
        QTest::qWait(100);
        QCOMPARE(countOf(*doc, AnnotType::PolyLine), 1);
    }

    void panelFillAndEndings() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("comment"), doc));
        CommentTool& tool = commentTool();
        QVERIFY(tool.panel() != nullptr);
        tool.start(CommentAction::Rectangle);
        QVERIFY(tool.fillSwatch()->isVisibleTo(tool.panel()));
        QVERIFY(!tool.lineEndBox()->isVisibleTo(tool.panel()));
        tool.fillSwatch()->presetButtons().at(2)->click();
        tool.fillSwatch()->paletteMenu()->close();
        const auto rectStyle = ot::comments::toolStyle(ot::Settings(), QStringLiteral("rectangle"));
        QVERIFY(rectStyle.fill.has_value());
        drag(v, {100, 300}, {220, 380});
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Square), 1, 10000);
        QCOMPARE(propsOf(*doc, ofType(*doc, AnnotType::Square).front().ref).interiorColor.toRgb(),
                 rectStyle.fill->toRgb());

        tool.start(CommentAction::Line);
        QVERIFY(tool.lineEndBox()->isVisibleTo(tool.panel()));
        QVERIFY(!tool.fillSwatch()->isVisibleTo(tool.panel()));
        tool.lineEndBox()->setCurrentIndex(
            tool.lineEndBox()->findData(static_cast<int>(LineEnding::ClosedArrow)));
        tool.lineStartBox()->setCurrentIndex(
            tool.lineStartBox()->findData(static_cast<int>(LineEnding::Circle)));
        drag(v, {100, 500}, {300, 520});
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Line), 1, 10000);
        const AnnotProps line = propsOf(*doc, ofType(*doc, AnnotType::Line).front().ref);
        QCOMPARE(line.lineEndStyle, LineEnding::ClosedArrow);
        QCOMPARE(line.lineStartStyle, LineEnding::Circle);

        bool rectangleKey = false;
        for (const ot::Command& c : tool.commands()) {
            if (c.id == QLatin1String("tool.rectangle")) {
                rectangleKey = c.shortcuts.size() == 1 && c.shortcuts.front() == QKeySequence(Qt::Key_R);
            }
        }
        QVERIFY(rectangleKey);
    }

    void vertexHandles() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        AnnotProps lp = ot::annot::toolDefaults(AnnotType::Line);
        lp.lineStart = {100, 300};
        lp.lineEnd = {250, 300};
        const auto line = ot::annot::create(doc->session(), 0, lp).result();
        AnnotProps pp = ot::annot::toolDefaults(AnnotType::Polygon);
        pp.vertices = {{300, 300}, {420, 300}, {360, 400}};
        const auto poly = ot::annot::create(doc->session(), 0, pp).result();
        v.tools().setTool(ViewToolId::AnnotSelect);
        AnnotSelectTool& tool = v.tools().annotSelect();

        // Конец линии — тянется маркером, ручек рамки нет.
        tool.setSelection(ofType(*doc, AnnotType::Line));
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(tool.vertexPoints().size()), 2, 10000);
        QVERIFY(!tool.handleAt(tool.frames().front().topLeft()).has_value());
        QVERIFY(tool.vertexAt(at(v, 250, 300)).has_value());
        drag(v, {250, 300}, {250, 380});
        QTRY_VERIFY_WITH_TIMEOUT(near(propsOf(*doc, line).lineEnd.y, 380, 2), 10000);
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Move vertex"));
        QVERIFY(near(propsOf(*doc, line).lineStart.x, 100, 0.5));

        // Вершина многоугольника; Shift — под 45° к соседней (первой) вершине.
        tool.setSelection(ofType(*doc, AnnotType::Polygon));
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(tool.vertexPoints().size()), 3, 10000);
        drag(v, {420, 300}, {480, 310}, Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(propsOf(*doc, poly).vertices[1].x > 450, 10000);
        const AnnotProps moved = propsOf(*doc, poly);
        QVERIFY2(near(moved.vertices[1].y, 300, 1.5), qPrintable(QString::number(moved.vertices[1].y)));
        doc->session().undo();
        QTRY_VERIFY_WITH_TIMEOUT(near(propsOf(*doc, poly).vertices[1].x, 420, 0.5), 10000);

        // Колено выноски.
        AnnotProps cp = ot::annot::toolDefaults(AnnotType::FreeText);
        cp.rect = {300, 500, 450, 530};
        cp.contents = "Callout";
        cp.intent = "FreeTextCallout";
        cp.callout = {{150, 600}, {280, 515}, {300, 515}};
        cp.lineStartStyle = LineEnding::OpenArrow;
        const auto callout = ot::annot::create(doc->session(), 0, cp).result();
        tool.setSelection(ofType(*doc, AnnotType::FreeText));
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(tool.vertexPoints().size()), 2, 10000);
        drag(v, {150, 600}, {120, 650});
        QTRY_VERIFY_WITH_TIMEOUT(near(propsOf(*doc, callout).callout[0].y, 650, 2), 10000);
        QCOMPARE(static_cast<int>(propsOf(*doc, callout).callout.size()), 3);
    }
};

OT_UI_TEST(ShapesTest, "shapes");
#include "shapes_test.moc"
