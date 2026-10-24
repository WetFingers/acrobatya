// ui.annot_select — «Выбор объектов» (план 2.2b; эталон 3.4 «Выделение и правка на странице»; основа
// comment-08): щелчок выбирает аннотацию annots-all.pdf (хит-тест annotAt задачей документа), перетаскивание
// двигает её /Rect (undo возвращает, рамка выбора идёт следом), ручка масштабирует; рамкой выбираются две
// аннотации, Del удаляет обе одним шагом; Shift+щелчок добавляет и убирает; правая кнопка выбирает и зовёт
// пустое контекстное меню (не показывается); Esc снимает выбор, потом возвращает «Выделение».
#include <QApplication>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <cmath>

#include "services/annot_ops.h"
#include "services/document_session.h"
#include "support/ui_test.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/view/tools/view_tool.h"

using ot::RectF;
using ot::annot::AnnotInfo;
using ot::annot::AnnotType;
using ot::ui::AnnotSelectTool;
using ot::ui::PageView;
using ot::ui::ToolController;
using ot::ui::ViewToolId;

namespace {

QString annotsPdf() { return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/annots/annots-all.pdf"); }

struct Opened {
    ot::DocumentSession session;
    PageView view;
};

void openAnnots(Opened& o) {
    QVERIFY(o.session.open(annotsPdf()));
    o.view.resize(1000, 800);
    o.view.setDocument(&o.session);
    o.view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&o.view));
    o.view.setZoomPercent(100);
    o.view.goToPage(0);
    o.view.setFocus();
    o.view.tools().setTool(ViewToolId::AnnotSelect);
}

std::vector<AnnotInfo> annots(Opened& o) { return ot::annot::list(o.session, 0).result(); }

AnnotInfo bySubtype(const std::vector<AnnotInfo>& list, AnnotType type) {
    for (const AnnotInfo& info : list) {
        if (info.type == type) {
            return info;
        }
    }
    return {};
}

// Точка страницы (пункты) → вьюпорт.
QPoint at(const PageView& v, double x, double y) { return v.pageTransform(0).map(QPointF(x, y)).toPoint(); }
QPoint center(const PageView& v, const RectF& r) { return at(v, (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2); }

bool near(const RectF& a, const RectF& b, double eps = 0.6) {
    return std::fabs(a.x0 - b.x0) < eps && std::fabs(a.y0 - b.y0) < eps && std::fabs(a.x1 - b.x1) < eps &&
           std::fabs(a.y1 - b.y1) < eps;
}

QString rectText(const RectF& r) {
    return QStringLiteral("[%1 %2 %3 %4]").arg(r.x0).arg(r.y0).arg(r.x1).arg(r.y1);
}

void drag(PageView& v, QPoint from, QPoint to, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QTest::mousePress(v.viewport(), Qt::LeftButton, mods, from);
    QTest::mouseMove(v.viewport(), (from + to) / 2);
    QTest::mouseMove(v.viewport(), to);
    QTest::mouseRelease(v.viewport(), Qt::LeftButton, mods, to);
}

int selectedCount(const AnnotSelectTool& tool) { return static_cast<int>(tool.selection().size()); }

}  // namespace

class AnnotSelectTest : public QObject {
    Q_OBJECT
private slots:

    void clickSelectsAndDragMovesWithUndo() {
        Opened o;
        openAnnots(o);
        PageView& v = o.view;
        AnnotSelectTool& tool = v.tools().annotSelect();
        QCOMPARE(tool.title(), QStringLiteral("Выбор объектов"));
        const AnnotInfo square = bySubtype(annots(o), AnnotType::Square);
        QVERIFY(square.ref.valid());

        QSignalSpy changed(&v.tools(), &ToolController::annotSelectionChanged);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, center(v, square.rect));
        QTRY_COMPARE_WITH_TIMEOUT(selectedCount(tool), 1, 10000);
        QCOMPARE(tool.selection().front().ref, square.ref);
        QVERIFY(changed.count() >= 1);
        QCOMPARE(static_cast<int>(v.annotationOverlay().rects(0).size()), 1);
        // Над выбранной — курсор перемещения, над ручкой — масштаба.
        QTest::mouseMove(v.viewport(), center(v, square.rect) + QPoint(2, 1));
        QCOMPARE(v.viewport()->cursor().shape(), Qt::SizeAllCursor);
        const QRectF frame = tool.frames().front();
        QVERIFY(tool.handleAt(frame.bottomRight()) == AnnotSelectTool::BottomRight);
        QTest::mouseMove(v.viewport(), frame.bottomRight().toPoint());
        QCOMPARE(v.viewport()->cursor().shape(), Qt::SizeFDiagCursor);

        // Перетаскивание на (40, 20) px — /Rect на (40, 20) / zoom пунктов.
        const QPoint from = center(v, square.rect);
        drag(v, from, from + QPoint(40, 20));
        QTRY_VERIFY_WITH_TIMEOUT(o.session.undoState().canUndo, 10000);
        QCOMPARE(o.session.undoState().undoName, QString::fromLatin1(ot::annot::kSetRectStep));
        const auto dx = static_cast<float>(40 / v.zoom());
        const auto dy = static_cast<float>(20 / v.zoom());
        const RectF expected{square.rect.x0 + dx, square.rect.y0 + dy, square.rect.x1 + dx,
                             square.rect.y1 + dy};
        const RectF moved = bySubtype(annots(o), AnnotType::Square).rect;
        QVERIFY2(near(moved, expected),
                 qPrintable(rectText(moved) + QStringLiteral(" против ") + rectText(expected)));
        QTRY_VERIFY_WITH_TIMEOUT(near(tool.selection().front().rect, moved, 0.01), 10000);
        QCOMPARE(selectedCount(tool), 1);

        // Undo возвращает /Rect, рамка выбора — следом (pageChanged → список заново).
        QVERIFY(o.session.undo().result());
        const RectF back = bySubtype(annots(o), AnnotType::Square).rect;
        QVERIFY2(near(back, square.rect, 0.01), qPrintable(rectText(back)));
        QTRY_VERIFY_WITH_TIMEOUT(near(tool.selection().front().rect, square.rect, 0.01), 10000);
        std::printf("OT_FEATURE:comment-08\n");
    }

    void handleResizes() {
        Opened o;
        openAnnots(o);
        PageView& v = o.view;
        AnnotSelectTool& tool = v.tools().annotSelect();
        const AnnotInfo circle = bySubtype(annots(o), AnnotType::Circle);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, center(v, circle.rect));
        QTRY_COMPARE_WITH_TIMEOUT(selectedCount(tool), 1, 10000);
        const QPoint corner = tool.frames().front().bottomRight().toPoint();
        drag(v, corner, corner + QPoint(30, 24));
        QTRY_VERIFY_WITH_TIMEOUT(o.session.undoState().canUndo, 10000);
        const RectF grown = bySubtype(annots(o), AnnotType::Circle).rect;
        const double zoom = v.zoom();
        QVERIFY2(std::fabs(grown.width() - (circle.rect.width() + 30 / zoom)) < 1.0,
                 qPrintable(rectText(grown)));
        QVERIFY2(std::fabs(grown.height() - (circle.rect.height() + 24 / zoom)) < 1.0,
                 qPrintable(rectText(grown)));
        QVERIFY(std::fabs(grown.x0 - circle.rect.x0) < 0.6);  // левый верхний угол на месте
        QVERIFY(std::fabs(grown.y0 - circle.rect.y0) < 0.6);
        // Ручку нельзя протащить за противоположную сторону: рамка не выворачивается.
        QTRY_VERIFY_WITH_TIMEOUT(near(tool.selection().front().rect, grown, 0.01), 10000);
        const QPoint left = tool.frames().front().topLeft().toPoint();
        const quint64 revision = o.session.revision();
        drag(v, left, left + QPoint(1000, 1000));
        QTRY_VERIFY_WITH_TIMEOUT(o.session.revision() > revision, 10000);
        const RectF squeezed = bySubtype(annots(o), AnnotType::Circle).rect;
        QVERIFY2(squeezed.width() > 0 && squeezed.height() > 0, qPrintable(rectText(squeezed)));
        // Вид круга после смены размера синтезирует MuPDF (5.1): рамка — с полями /RD на толщину линии.
        QVERIFY2(std::fabs(squeezed.x1 - grown.x1) < 1.5, qPrintable(rectText(squeezed) + rectText(grown)));
    }

    void marqueeSelectsTwoAndDeleteRemovesBoth() {
        Opened o;
        openAnnots(o);
        PageView& v = o.view;
        AnnotSelectTool& tool = v.tools().annotSelect();
        const std::vector<AnnotInfo> list = annots(o);
        QCOMPARE(static_cast<int>(list.size()), 28);
        const AnnotInfo square = bySubtype(list, AnnotType::Square);
        const AnnotInfo circle = bySubtype(list, AnnotType::Circle);
        // Рамка от пустого места левее квадрата до середины круга (выше и ниже ряда — пусто).
        const QPoint from = at(v, 20, square.rect.y0 - 8);
        const QPoint to = at(v, (circle.rect.x0 + circle.rect.x1) / 2, square.rect.y1 + 6);
        drag(v, from, to);
        QTRY_COMPARE_WITH_TIMEOUT(selectedCount(tool), 2, 10000);
        QVERIFY(tool.isSelected(square.ref));
        QVERIFY(tool.isSelected(circle.ref));
        QCOMPARE(static_cast<int>(v.annotationOverlay().rects(0).size()), 2);
        QVERIFY(!tool.handleAt(tool.frames().front().topLeft()).has_value());  // ручки — у одной выбранной

        // Del — обе одним шагом истории.
        QTest::keyClick(&v, Qt::Key_Delete);
        QCOMPARE(selectedCount(tool), 0);
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(annots(o).size()), 26, 10000);
        QCOMPARE(o.session.undoState().undoName, QString::fromLatin1(ot::annot::kDeleteStep));
        QVERIFY(o.session.undo().result());
        QCOMPARE(static_cast<int>(annots(o).size()), 28);
        QVERIFY(!o.session.undoState().canUndo);
    }

    void shiftClickTogglesAndClickOnEmptyClears() {
        Opened o;
        openAnnots(o);
        PageView& v = o.view;
        AnnotSelectTool& tool = v.tools().annotSelect();
        const std::vector<AnnotInfo> list = annots(o);
        const AnnotInfo square = bySubtype(list, AnnotType::Square);
        const AnnotInfo stamp = bySubtype(list, AnnotType::Stamp);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, center(v, square.rect));
        QTRY_COMPARE_WITH_TIMEOUT(selectedCount(tool), 1, 10000);
        QTest::qWait(QApplication::doubleClickInterval() + 50);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, Qt::ShiftModifier, center(v, stamp.rect));
        QTRY_COMPARE_WITH_TIMEOUT(selectedCount(tool), 2, 10000);
        QTest::qWait(QApplication::doubleClickInterval() + 50);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, Qt::ShiftModifier, center(v, square.rect));
        QTRY_COMPARE_WITH_TIMEOUT(selectedCount(tool), 1, 10000);
        QCOMPARE(tool.selection().front().ref, stamp.ref);
        // Щелчок по пустому месту снимает выбор.
        QTest::qWait(QApplication::doubleClickInterval() + 50);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 20, square.rect.y0 - 8));
        QTRY_COMPARE_WITH_TIMEOUT(selectedCount(tool), 0, 10000);
        QVERIFY(v.annotationOverlay().empty());
        QVERIFY(!o.session.undoState().canUndo);  // щелчки ничего не правят
    }

    void rightClickSelectsAndAsksForMenu() {
        Opened o;
        openAnnots(o);
        PageView& v = o.view;
        AnnotSelectTool& tool = v.tools().annotSelect();
        const AnnotInfo stamp = bySubtype(annots(o), AnnotType::Stamp);
        QSignalSpy menus(&v.tools(), &ToolController::annotContextMenu);
        QTest::mouseClick(v.viewport(), Qt::RightButton, {}, center(v, stamp.rect));
        QTRY_COMPARE_WITH_TIMEOUT(menus.count(), 1, 10000);
        QCOMPARE(selectedCount(tool), 1);
        QCOMPARE(tool.selection().front().ref, stamp.ref);
        QVERIFY(QApplication::activePopupWidget() == nullptr);  // пустое меню не показано
    }

    void escapeClearsThenLeavesTool() {
        Opened o;
        openAnnots(o);
        PageView& v = o.view;
        AnnotSelectTool& tool = v.tools().annotSelect();
        const AnnotInfo square = bySubtype(annots(o), AnnotType::Square);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, center(v, square.rect));
        QTRY_COMPARE_WITH_TIMEOUT(selectedCount(tool), 1, 10000);
        QTest::keyClick(&v, Qt::Key_Escape);
        QCOMPARE(selectedCount(tool), 0);
        QCOMPARE(v.tools().tool(), ViewToolId::AnnotSelect);
        QTest::keyClick(&v, Qt::Key_Escape);
        QCOMPARE(v.tools().tool(), ViewToolId::Select);
    }
};

OT_UI_TEST(AnnotSelectTest, "annot_select");
#include "annot_select_test.moc"
