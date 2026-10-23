// ui.tools — инструменты просмотра PageView (план 2.2; эталон 3.1 «Масштаб», 3.2, 6.8; view-05, view-33,
// comment-42): «Выделение» мышью выделяет две строки search.pdf (текст — по core/text/selection.h), двойной и
// тройной щелчок — слово и строка, Alt — столбцы, Ctrl+A — всё, I-курсор над текстом; «Рука» прокручивает;
// «Увеличение области» — рамка во всё окно и щелчок; «Снимок» кладёт растр в буфер и возвращает прежний
// инструмент, если не закреплён; «Лупа» и «Панорамирование и масштаб» — панели; однобуквенные клавиши
// выключены по умолчанию, Shift+Z — цикл зума, пробел — временная «Рука», Esc — сброс.
#include <QApplication>
#include <QClipboard>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSlider>
#include <QtTest/QtTest>

#include <cmath>

#include "services/document_session.h"
#include "support/ui_test.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/view/tools/tool_panels.h"
#include "ui/view/tools/view_tool.h"
#include "ui/widgets/tool_button.h"

using ot::ui::PageView;
using ot::ui::ToolController;
using ot::ui::ViewToolId;

namespace {

QString searchPdf() { return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/text/search.pdf"); }

// Точка страницы (пункты) → вьюпорт.
QPoint at(const PageView& v, int page, double x, double y) {
    return v.pageTransform(page).map(QPointF(x, y)).toPoint();
}

struct Opened {
    ot::DocumentSession session;
    PageView view;
};

void openSearch(Opened& o) {
    QVERIFY(o.session.open(searchPdf()));
    o.view.resize(1000, 800);
    o.view.setDocument(&o.session);
    o.view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&o.view));
    o.view.setZoomPercent(100);
    o.view.goToPage(0);
    o.view.setFocus();
}

// Снимок вида — build/<пресет>/ui-shots/<имя>.png (смотреть глазами).
void saveShot(QWidget& w, const QString& name) {
    const QString dir = QStringLiteral(OTTISK_BINARY_DIR "/ui-shots");
    QDir().mkpath(dir);
    w.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
}

void drag(PageView& v, QPoint from, QPoint to, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QTest::mousePress(v.viewport(), Qt::LeftButton, mods, from);
    const QPoint mid = (from + to) / 2;
    QTest::mouseMove(v.viewport(), mid);
    QTest::mouseMove(v.viewport(), to);
    QTest::mouseRelease(v.viewport(), Qt::LeftButton, mods, to);
}

}  // namespace

class ToolsTest : public QObject {
    Q_OBJECT
private slots:

    void controllerDefaultsAndKeys() {
        PageView v;
        v.resize(800, 600);
        v.setPageSizes(QVector<QSizeF>(3, QSizeF(595, 842)));
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.setFocus();
        ToolController& tools = v.tools();
        QCOMPARE(tools.tool(), ViewToolId::Select);
        QVERIFY(!tools.singleKeyShortcuts());
        // Однобуквенные выключены по умолчанию (эталон 6.8).
        QTest::keyClick(&v, Qt::Key_H);
        QCOMPARE(tools.tool(), ViewToolId::Select);
        tools.setSingleKeyShortcuts(true);
        QSignalSpy changed(&tools, &ToolController::toolChanged);
        QTest::keyClick(&v, Qt::Key_H);
        QCOMPARE(tools.tool(), ViewToolId::Hand);
        QTest::keyClick(&v, Qt::Key_Z);
        QCOMPARE(tools.tool(), ViewToolId::MarqueeZoom);
        QTest::keyClick(&v, Qt::Key_Z, Qt::ShiftModifier);
        QCOMPARE(tools.tool(), ViewToolId::Loupe);
        QTest::keyClick(&v, Qt::Key_Z, Qt::ShiftModifier);
        QCOMPARE(tools.tool(), ViewToolId::PanZoom);
        QTest::keyClick(&v, Qt::Key_Z, Qt::ShiftModifier);
        QCOMPARE(tools.tool(), ViewToolId::MarqueeZoom);
        QTest::keyClick(&v, Qt::Key_X);
        QCOMPARE(tools.tool(), ViewToolId::Snapshot);
        QTest::keyClick(&v, Qt::Key_V);
        QCOMPARE(tools.tool(), ViewToolId::Select);
        QVERIFY(changed.count() >= 6);
        // Esc возвращает «Выделение».
        tools.setTool(ViewToolId::Hand);
        QTest::keyClick(&v, Qt::Key_Escape);
        QCOMPARE(tools.tool(), ViewToolId::Select);
        // Пробел — «Рука» на время нажатия.
        QTest::keyPress(&v, Qt::Key_Space);
        QCOMPARE(tools.tool(), ViewToolId::Hand);
        QTest::keyRelease(&v, Qt::Key_Space);
        QCOMPARE(tools.tool(), ViewToolId::Select);
        // Названия по-русски.
        QCOMPARE(tools.toolFor(ViewToolId::Hand).title(), QStringLiteral("Рука"));
        QCOMPARE(tools.toolFor(ViewToolId::MarqueeZoom).title(), QStringLiteral("Увеличение области"));
        QCOMPARE(tools.toolFor(ViewToolId::Snapshot).title(), QStringLiteral("Снимок"));
    }

    void handDragScrolls() {
        PageView v;
        v.resize(800, 600);
        v.setPageSizes(QVector<QSizeF>(5, QSizeF(595, 842)));
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.setZoomPercent(200);
        v.tools().setTool(ViewToolId::Hand);
        v.horizontalScrollBar()->setValue(300);
        v.verticalScrollBar()->setValue(500);
        QCOMPARE(v.viewport()->cursor().shape(), Qt::OpenHandCursor);
        QTest::mousePress(v.viewport(), Qt::LeftButton, {}, QPoint(400, 300));
        QCOMPARE(v.viewport()->cursor().shape(), Qt::ClosedHandCursor);
        QTest::mouseMove(v.viewport(), QPoint(300, 200));
        QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, QPoint(300, 200));
        QCOMPARE(v.horizontalScrollBar()->value(), 400);
        QCOMPARE(v.verticalScrollBar()->value(), 600);
        std::printf("OT_FEATURE:view-33\n");
    }

    // Протягивание мышью от начала первой строки до конца второй — обе строки (selection.h).
    void selectDragsTwoLines() {
        Opened o;
        openSearch(o);
        PageView& v = o.view;
        auto& select = v.tools().select();
        QCOMPARE(v.tools().tool(), ViewToolId::Select);
        // Наведение загружает слой страницы; над строкой — I-курсор, над полем — стрелка.
        QTest::mouseMove(v.viewport(), at(v, 0, 150, 257));
        QTRY_VERIFY_WITH_TIMEOUT(select.hasTextPage(0), 10000);
        QTest::mouseMove(v.viewport(), at(v, 0, 152, 257));  // та же точка второй раз событием не придёт
        QCOMPARE(v.viewport()->cursor().shape(), Qt::IBeamCursor);
        QTest::mouseMove(v.viewport(), at(v, 0, 500, 400));  // поле справа от строк
        QCOMPARE(v.viewport()->cursor().shape(), Qt::ArrowCursor);

        QSignalSpy changed(&v.tools(), &ToolController::selectionChanged);
        drag(v, at(v, 0, 72.5, 257), at(v, 0, 250, 274));
        QCOMPARE(select.selectedText(),
                 QStringLiteral("Первая строка для выделения.\nВторая строка для выделения."));
        QVERIFY(changed.count() >= 1);
        QVERIFY(!v.selectionOverlay().quads(0).empty());
        // Esc снимает выделение.
        QTest::keyClick(&v, Qt::Key_Escape);
        QVERIFY(!select.hasSelection());
        QVERIFY(v.selectionOverlay().empty());
        std::printf("OT_FEATURE:view-33\n");
    }

    void selectWordLineColumnsAndAll() {
        Opened o;
        openSearch(o);
        PageView& v = o.view;
        auto& select = v.tools().select();
        QTest::mouseMove(v.viewport(), at(v, 0, 100, 257));
        QTRY_VERIFY_WITH_TIMEOUT(select.hasTextPage(0), 10000);
        // Двойной щелчок по слову «строка» первой строки — слово.
        const QPoint word = at(v, 0, 130, 258);
        QTest::mouseDClick(v.viewport(), Qt::LeftButton, {}, word);
        QCOMPARE(select.selectedText(), QStringLiteral("строка"));
        // Тройной — строка целиком.
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, word);
        QCOMPARE(select.selectedText(), QStringLiteral("Первая строка для выделения."));
        QTest::qWait(QApplication::doubleClickInterval() + 50);
        // Alt — прямоугольник: начала двух строк.
        drag(v, at(v, 0, 70, 250), at(v, 0, 110, 280), Qt::AltModifier);
        const QString columns = select.selectedText();
        QVERIFY2(columns.startsWith(QStringLiteral("Перв")), qPrintable(columns));
        QVERIFY2(columns.contains(QStringLiteral("\nВтор")), qPrintable(columns));
        QVERIFY2(!columns.contains(QStringLiteral("выделения")), qPrintable(columns));
        // Ctrl+A — весь документ (слои страниц приходят по мере извлечения).
        QTest::keyClick(&v, Qt::Key_A, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(
            select.selectedText().contains(QStringLiteral("Третья страница без искомого слова.")), 10000);
        QVERIFY(select.selectedText().startsWith(QStringLiteral("Оттиск: поиск и выделение текста")));
        QCOMPARE(static_cast<int>(select.selection().size()), 3);
    }

    // Рамка — во всё окно, её центр — в центр окна; щелчок — шаг вверх, Ctrl+щелчок — вниз.
    void marqueeZoomFitsBand() {
        PageView v;
        v.resize(800, 600);
        v.setPageSizes(QVector<QSizeF>(5, QSizeF(595, 842)));
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.setZoomPercent(100);
        v.goToPage(1);
        v.tools().setTool(ViewToolId::MarqueeZoom);
        const QRect band(200, 150, 200, 100);
        const int page = v.pageAtViewport(band.center());
        const QPointF centerPt = v.viewportToPage(page, QPointF(band.center()) + QPointF(0.5, 0.5));
        const float before = v.zoom();
        const double expected =
            std::min(v.viewport()->width() / 200.0, v.viewport()->height() / 100.0);  // по меньшей стороне
        drag(v, band.topLeft(), band.bottomRight() + QPoint(1, 1));
        QVERIFY2(std::fabs(v.zoom() / before - expected) < 0.02 * expected,
                 qPrintable(QStringLiteral("%1 против %2").arg(v.zoom() / before).arg(expected)));
        const QPointF now = v.pageTransform(page).map(centerPt);
        const QPointF mid(v.viewport()->width() / 2.0, v.viewport()->height() / 2.0);
        QVERIFY2(std::hypot(now.x() - mid.x(), now.y() - mid.y()) < 3.0,
                 qPrintable(QStringLiteral("%1,%2").arg(now.x()).arg(now.y())));
        QCOMPARE(v.tools().tool(), ViewToolId::MarqueeZoom);  // остаётся выбранным
        v.setZoomPercent(100);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, QPoint(400, 300));
        QCOMPARE(qRound(v.zoomPercent()), 125);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, Qt::ControlModifier, QPoint(400, 300));
        QCOMPARE(qRound(v.zoomPercent()), 100);
        std::printf("OT_FEATURE:view-05\n");
    }

    // Снимок: растр в буфер (размер области при разрешении экрана, Alt — 300 dpi); однократный — назад к
    // прежнему инструменту; закреплённый — остаётся (comment-42).
    void snapshotToClipboardAndPin() {
        Opened o;
        openSearch(o);
        PageView& v = o.view;
        ToolController& tools = v.tools();
        tools.setTool(ViewToolId::Hand);
        tools.setTool(ViewToolId::Snapshot);
        QSignalSpy shots(&tools, &ToolController::snapshotTaken);
        const QPoint a = at(v, 0, 72, 240);
        const QPoint b = at(v, 0, 272, 290);
        drag(v, a, b);
        QTRY_COMPARE_WITH_TIMEOUT(shots.count(), 1, 10000);
        const QImage image = shots.first().first().value<QImage>();
        QVERIFY(std::abs(image.width() - (b.x() - a.x())) <= 2);
        QVERIFY(std::abs(image.height() - (b.y() - a.y())) <= 2);
        QCOMPARE(tools.tool(), ViewToolId::Hand);  // однократный: назад к прежнему
        const QImage clip = QApplication::clipboard()->image();
        QVERIFY(!clip.isNull());
        QCOMPARE(clip.size(), image.size());

        QSignalSpy pinned(&tools, &ToolController::pinnedChanged);
        tools.setPinned(true);
        QCOMPARE(pinned.count(), 1);
        tools.setTool(ViewToolId::Snapshot);
        drag(v, a, b, Qt::AltModifier);
        QTRY_COMPARE_WITH_TIMEOUT(shots.count(), 2, 10000);
        const QImage high = shots.last().first().value<QImage>();
        // 300 dpi против 96: втрое с лишним больше.
        QVERIFY2(std::fabs(high.width() / static_cast<double>(image.width()) - 300.0 / 96.0) < 0.05,
                 qPrintable(QString::number(high.width())));
        QCOMPARE(tools.tool(), ViewToolId::Snapshot);  // закреплён — остаётся
        std::printf("OT_FEATURE:comment-42\n");
        std::printf("OT_FEATURE:view-33\n");
    }

    void loupeShowsMagnifiedArea() {
        Opened o;
        openSearch(o);
        PageView& v = o.view;
        ToolController& tools = v.tools();
        tools.setTool(ViewToolId::Loupe);
        auto* panel = v.findChild<ot::ui::LoupePanel*>(QStringLiteral("loupePanel"));
        QVERIFY(panel != nullptr);
        QVERIFY(panel->isVisible());
        QCOMPARE(panel->slider()->minimum(), 200);
        QCOMPARE(panel->slider()->maximum(), 800);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 0, 150, 258));
        QCOMPARE(tools.loupe().page(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!panel->image().isNull(), 10000);
        const QRectF area400 = tools.loupe().area();
        QVERIFY(area400.contains(QPointF(150, 258)));
        panel->slider()->setValue(800);
        QCOMPARE(tools.loupe().percent(), 800);
        QVERIFY(std::fabs(tools.loupe().area().width() - area400.width() / 2) < 0.5);
        // Картинка лупы — размер панели при её масштабе: на 800 % строка видна крупно.
        QTRY_VERIFY_WITH_TIMEOUT(!panel->image().isNull(), 10000);
        QTest::qWait(50);
        saveShot(v, QStringLiteral("tools_loupe"));
        QTest::keyClick(&v, Qt::Key_Escape);
        QCOMPARE(tools.tool(), ViewToolId::Select);
        QVERIFY(!panel->isVisible());
        std::printf("OT_FEATURE:view-05\n");
    }

    void panZoomMovesView() {
        Opened o;
        openSearch(o);
        PageView& v = o.view;
        v.setZoomPercent(400);
        v.goToPage(0);
        ToolController& tools = v.tools();
        tools.setTool(ViewToolId::PanZoom);
        auto* panel = v.findChild<ot::ui::PanZoomPanel*>(QStringLiteral("panZoomPanel"));
        QVERIFY(panel != nullptr);
        QVERIFY(panel->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!panel->thumbnail().isNull(), 10000);
        QVERIFY(!panel->frame().isEmpty());
        QVERIFY(panel->frame().width() < 0.5);  // при 400 % видна малая часть страницы
        // Щелчок в правый нижний угол миниатюры — вид туда.
        auto* thumb = panel->thumbnailWidget();
        const QRectF img = thumb->imageRect();
        const int x = v.horizontalScrollBar()->value();
        const int y = v.verticalScrollBar()->value();
        QTest::mouseClick(thumb, Qt::LeftButton, {},
                          (img.topLeft() + QPointF(img.width() * 0.8, img.height() * 0.8)).toPoint());
        QVERIFY(v.horizontalScrollBar()->value() > x);
        QVERIFY(v.verticalScrollBar()->value() > y);
        QVERIFY(panel->frame().center().x() > 0.6);
        saveShot(v, QStringLiteral("tools_pan_zoom"));
        // Кнопки страниц и масштаба.
        auto* next = panel->findChild<ot::ui::ToolButton*>(QStringLiteral("panZoomNext"));
        QVERIFY(next != nullptr);
        next->click();
        QCOMPARE(v.currentPage(), 1);
        auto* out = panel->findChild<ot::ui::ToolButton*>(QStringLiteral("panZoomOut"));
        QVERIFY(out != nullptr);
        out->click();
        QCOMPARE(qRound(v.zoomPercent()), 300);
        // На странице — как «Рука».
        const int before = v.verticalScrollBar()->value();
        QTest::mousePress(v.viewport(), Qt::LeftButton, {}, QPoint(500, 500));
        QTest::mouseMove(v.viewport(), QPoint(500, 400));
        QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, QPoint(500, 400));
        QCOMPARE(v.verticalScrollBar()->value(), before + 100);
        tools.setTool(ViewToolId::Select);
        QVERIFY(!panel->isVisible());
        std::printf("OT_FEATURE:view-05\n");
    }
};

OT_UI_TEST(ToolsTest, "tools");
#include "tools_test.moc"
