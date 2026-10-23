// ui.page_view_layout — раскладка и прокрутка PageView (план 2.2; эталон 3.1, 6.8; view-03, view-04,
// view-06…09): layoutPages() для пяти режимов и поворота вида против ожиданий
// tests/fixtures/view/layout_expected.json (посчитаны вручную), зум относительно курсора держит точку
// страницы (±1 px), «по ширине» — ровно ширина вьюпорта, история видов, клавиши навигации и масштаба,
// постраничные режимы, 1 200 страниц лениво, автопрокрутка, /PageLayout файла.
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollBar>
#include <QSignalSpy>
#include <QWheelEvent>
#include <QtTest/QtTest>

#include <cmath>

#include "core/render/fit.h"
#include "services/document_session.h"
#include "support/ui_test.h"
#include "ui/view/layout.h"
#include "ui/view/page_view.h"

using ot::FitMode;
using ot::ui::LayoutOptions;
using ot::ui::PageLayout;
using ot::ui::PageMode;
using ot::ui::PageView;

namespace {

QVector<QSizeF> a4Pages(int n) { return QVector<QSizeF>(n, QSizeF(595.2756, 841.8898)); }

QJsonObject loadExpected() {
    QFile f(QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/view/layout_expected.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(f.readAll()).object();
}

PageMode modeFromName(const QString& name) {
    if (name == QLatin1String("SinglePage")) {
        return PageMode::SinglePage;
    }
    if (name == QLatin1String("Facing")) {
        return PageMode::Facing;
    }
    if (name == QLatin1String("FacingContinuous")) {
        return PageMode::FacingContinuous;
    }
    return PageMode::Continuous;
}

double distance(QPointF a, QPointF b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }

void ctrlWheel(PageView& v, QPointF at, int delta) {
    QWheelEvent ev(at, v.viewport()->mapToGlobal(at), QPoint(), QPoint(0, delta), Qt::NoButton,
                   Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(v.viewport(), &ev);
}

void wheel(PageView& v, QPointF at, int delta) {
    QWheelEvent ev(at, v.viewport()->mapToGlobal(at), QPoint(), QPoint(0, delta), Qt::NoButton,
                   Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(v.viewport(), &ev);
}

}  // namespace

class PageViewLayoutTest : public QObject {
    Q_OBJECT
private slots:

    // Пять режимов и поворот вида: ряды, рамки страниц и размер содержимого — как в JSON.
    void layoutMatchesExpectedJson() {
        const QJsonObject want = loadExpected();
        QVERIFY2(!want.isEmpty(), "tests/fixtures/view/layout_expected.json");
        QVector<QSizeF> sizes;
        for (const QJsonValue& p : want[QStringLiteral("pages_pt")].toArray()) {
            sizes << QSizeF(p.toArray()[0].toDouble(), p.toArray()[1].toDouble());
        }
        const auto cases = want[QStringLiteral("cases")].toArray();
        QCOMPARE(cases.size(), 6);
        for (const QJsonValue& cv : cases) {
            const QJsonObject c = cv.toObject();
            const QString name = c[QStringLiteral("name")].toString();
            LayoutOptions o;
            o.mode = modeFromName(c[QStringLiteral("mode")].toString());
            o.coverPage = c[QStringLiteral("cover")].toBool();
            o.rotation = c[QStringLiteral("rotation")].toInt();
            o.zoom = static_cast<float>(want[QStringLiteral("zoom")].toDouble());
            o.gap = static_cast<float>(want[QStringLiteral("gap")].toDouble());
            const PageLayout layout = ot::ui::layoutPages(sizes, o);

            const QJsonArray rows = c[QStringLiteral("rows")].toArray();
            QVERIFY2(layout.rowCount() == rows.size(), qPrintable(name));
            for (int r = 0; r < rows.size(); ++r) {
                const QJsonArray pages = rows[r].toArray();
                QVERIFY2(layout.rows()[static_cast<std::size_t>(r)].first == pages[0].toInt(),
                         qPrintable(name));
                QVERIFY2(layout.rows()[static_cast<std::size_t>(r)].count == pages.size(), qPrintable(name));
            }
            const QJsonArray rects = c[QStringLiteral("rects")].toArray();
            for (int i = 0; i < rects.size(); ++i) {
                const QJsonArray e = rects[i].toArray();
                const QRectF expected(e[0].toDouble(), e[1].toDouble(), e[2].toDouble(), e[3].toDouble());
                const QRectF got = layout.slot(i).rect;
                const QString what = QStringLiteral("%1, страница %2: %3,%4 %5×%6")
                                         .arg(name)
                                         .arg(i)
                                         .arg(got.x())
                                         .arg(got.y())
                                         .arg(got.width())
                                         .arg(got.height());
                QVERIFY2(std::fabs(got.x() - expected.x()) < 0.01 &&
                             std::fabs(got.y() - expected.y()) < 0.01 &&
                             std::fabs(got.width() - expected.width()) < 0.01 &&
                             std::fabs(got.height() - expected.height()) < 0.01,
                         qPrintable(what));
            }
            const QJsonArray content = c[QStringLiteral("content")].toArray();
            for (int r = 0; r < content.size(); ++r) {
                const QSizeF s = layout.contentSize(r);
                const QJsonArray e = content[r].toArray();
                QVERIFY2(
                    std::fabs(s.width() - e[0].toDouble()) < 0.01 &&
                        std::fabs(s.height() - e[1].toDouble()) < 0.01,
                    qPrintable(
                        QStringLiteral("%1, ряд %2: %3×%4").arg(name).arg(r).arg(s.width()).arg(s.height())));
            }
            // pageAt: центр страницы — она, зазор — никто.
            for (int i = 0; i < layout.pageCount(); ++i) {
                QCOMPARE(layout.pageAt(layout.slot(i).rect.center(), layout.slot(i).row), i);
            }
            QCOMPARE(layout.pageAt(QPointF(1, 1)), -1);
        }
        std::printf("OT_FEATURE:view-06\n");
    }

    void pdfPageLayoutNames() {
        QCOMPARE(ot::ui::pageModeFromPdf(QStringLiteral("SinglePage"))->mode, PageMode::SinglePage);
        QCOMPARE(ot::ui::pageModeFromPdf(QStringLiteral("OneColumn"))->mode, PageMode::Continuous);
        QCOMPARE(ot::ui::pageModeFromPdf(QStringLiteral("TwoColumnLeft"))->mode, PageMode::FacingContinuous);
        QVERIFY(!ot::ui::pageModeFromPdf(QStringLiteral("TwoColumnLeft"))->coverPage);
        QVERIFY(ot::ui::pageModeFromPdf(QStringLiteral("TwoColumnRight"))->coverPage);
        QCOMPARE(ot::ui::pageModeFromPdf(QStringLiteral("TwoPageLeft"))->mode, PageMode::Facing);
        QVERIFY(ot::ui::pageModeFromPdf(QStringLiteral("TwoPageRight"))->coverPage);
        QVERIFY(!ot::ui::pageModeFromPdf(QString()).has_value());
        QVERIFY(!ot::ui::pageModeFromPdf(QStringLiteral("Nonsense")).has_value());
    }

    // 1 200 страниц: раскладка — по размерам за миллисекунды, видимых — единицы.
    void thousandPagesAreLazy() {
        const QVector<QSizeF> sizes = a4Pages(1200);
        QElapsedTimer t;
        t.start();
        LayoutOptions o;
        const PageLayout layout = ot::ui::layoutPages(sizes, o);
        QVERIFY2(t.elapsed() < 200, qPrintable(QString::number(t.elapsed())));
        QCOMPARE(layout.pageCount(), 1200);
        const auto visible = layout.pagesIn(QRectF(0, 500000, 800, 600));
        QVERIFY(!visible.empty());
        QVERIFY(visible.size() <= 2);

        PageView v;
        v.resize(800, 600);
        v.setPageSizes(sizes);
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        QVERIFY(v.visiblePages().size() <= 2);
        v.goToPage(1199);
        QCOMPARE(v.currentPage(), 1199);
        const auto last = v.visiblePages();
        QVERIFY(std::find(last.begin(), last.end(), 1199) != last.end());
    }

    // Ctrl+колесо под курсором: точка страницы под курсором остаётся под ним (±1 px), туда и обратно.
    void wheelZoomKeepsPointUnderCursor() {
        // Содержимое шире и выше окна при всех масштабах проверки (уже окна — оно по центру, якорь по x не
        // держится — как в Acrobat).
        PageView v;
        v.resize(700, 500);
        v.setPageSizes(a4Pages(10));
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.setZoomPercent(100);
        v.goToPage(2);
        QSignalSpy zoomed(&v, &PageView::zoomChanged);
        for (const QPointF cursor : {QPointF(523, 311), QPointF(130, 470), QPointF(650, 90)}) {
            for (const int delta : {120, 240, -120, -240, 120, -120}) {
                const int page = v.pageAtViewport(cursor);
                QVERIFY(page >= 0);
                const QPointF pt = v.viewportToPage(page, cursor);
                const float before = v.zoom();
                ctrlWheel(v, cursor, delta);
                QVERIFY(v.zoom() != before);
                const QPointF after = v.pageTransform(page).map(pt);
                QVERIFY2(distance(after, cursor) <= 1.0, qPrintable(QStringLiteral("%1,%2 → %3,%4 при %5 %")
                                                                        .arg(cursor.x())
                                                                        .arg(cursor.y())
                                                                        .arg(after.x())
                                                                        .arg(after.y())
                                                                        .arg(v.zoomPercent())));
            }
        }
        QVERIFY(zoomed.count() > 0);
        // Явный масштаб с якорем — так же.
        const QPointF anchor(400, 300);
        const int page = v.pageAtViewport(anchor);
        const QPointF pt = v.viewportToPage(page, anchor);
        v.setZoomPercent(400, anchor);
        QVERIFY(distance(v.pageTransform(page).map(pt), anchor) <= 1.0);
        QCOMPARE(qRound(v.zoomPercent()), 400);
        std::printf("OT_FEATURE:view-03\n");
    }

    void zoomRangeAndPresets() {
        PageView v;
        v.resize(800, 600);
        v.setPageSizes(a4Pages(3));
        v.show();
        v.setZoomPercent(1);
        QCOMPARE(qRound(v.zoomPercent()), 8);
        v.setZoomPercent(100000);
        QCOMPARE(qRound(v.zoomPercent()), 6400);
        v.setZoomPercent(100);
        v.zoomIn();
        QCOMPARE(qRound(v.zoomPercent()), 125);
        v.zoomOut();
        v.zoomOut();
        QCOMPARE(qRound(v.zoomPercent()), 75);
        v.setZoomPercent(110);
        v.zoomIn();
        QCOMPARE(qRound(v.zoomPercent()), 125);
        // Проценты — относительно dpi: 100 % при 144 dpi — 2 px на пункт.
        v.setScreenDpi(144);
        v.setZoomPercent(100);
        QVERIFY(std::fabs(v.zoom() - 2.0f) < 0.001f);
    }

    // «По ширине»: ширина содержимого — ровно ширина вьюпорта, горизонтальной полосы нет; липкая подгонка.
    void fitWidthEqualsViewport() {
        PageView v;
        v.resize(900, 640);
        v.setPageSizes(a4Pages(6));
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.fit(FitMode::Width);
        QCOMPARE(v.fitMode(), std::optional<FitMode>(FitMode::Width));
        QVERIFY(std::fabs(v.pageLayout().contentSize().width() - v.viewport()->width()) <= 1.0);
        QCOMPARE(v.horizontalScrollBar()->maximum(), 0);
        QVERIFY(v.verticalScrollBar()->maximum() > 0);
        // Окно шире — подгонка держится.
        v.resize(1100, 640);
        QTest::qWait(1);
        QVERIFY(std::fabs(v.pageLayout().contentSize().width() - v.viewport()->width()) <= 1.0);
        // Разворот: две страницы и три зазора — ширина вьюпорта.
        v.setPageMode(PageMode::FacingContinuous);
        v.fit(FitMode::Width);
        QVERIFY(std::fabs(v.pageLayout().contentSize().width() - v.viewport()->width()) <= 1.0);
        QCOMPARE(v.horizontalScrollBar()->maximum(), 0);
        // Страница целиком: помещается в окно по обеим сторонам.
        v.setPageMode(PageMode::Continuous);
        v.goToPage(3);
        v.fit(FitMode::Page);
        const QRectF page = v.pageViewportRect(3);
        QVERIFY(page.height() <= v.viewport()->height());
        QVERIFY(page.top() >= 0 && page.bottom() <= v.viewport()->height() + 0.5);
        QCOMPARE(v.currentPage(), 3);
        // Явный масштаб снимает подгонку.
        v.setZoomPercent(50);
        QVERIFY(!v.fitMode().has_value());
        // Реальный размер — 100 %.
        v.fit(FitMode::ActualSize);
        QCOMPARE(qRound(v.zoomPercent()), 100);
        std::printf("OT_FEATURE:view-04\n");
    }

    // История: «Назад» возвращает прежний вид (страница, масштаб, прокрутка), «Вперёд» — обратно; не
    // больше 50.
    void historyBackRestoresPreviousView() {
        PageView v;
        v.resize(800, 600);
        v.setPageSizes(a4Pages(12));
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        const float initial = v.zoom();
        v.setZoomPercent(100);
        v.goToPage(5);
        const ot::ui::ViewState at5 = v.viewState();
        v.fit(FitMode::Width);
        QVERIFY(v.history().canBack());
        QVERIFY(v.goBack());
        QCOMPARE(v.currentPage(), 5);
        QCOMPARE(qRound(v.zoomPercent()), 100);
        QCOMPARE(v.viewState().scroll, at5.scroll);
        QVERIFY(v.goBack());
        QCOMPARE(v.currentPage(), 0);
        QCOMPARE(qRound(v.zoomPercent()), 100);
        QVERIFY(v.goBack());
        QCOMPARE(v.zoom(), initial);
        QVERIFY(!v.goBack());
        QVERIFY(v.goForward());
        QVERIFY(v.goForward());
        QCOMPARE(v.currentPage(), 5);
        // Alt+← / Alt+→.
        QTest::keyClick(&v, Qt::Key_Left, Qt::AltModifier);
        QCOMPARE(v.currentPage(), 0);
        QTest::keyClick(&v, Qt::Key_Right, Qt::AltModifier);
        QCOMPARE(v.currentPage(), 5);
        // Стек ограничен 50 видами.
        for (int i = 0; i < 80; ++i) {
            v.goToPage(i % 2 == 0 ? 1 : 2);
        }
        QCOMPARE(v.history().size(), ot::ui::ViewHistory::kMaxStates);
        std::printf("OT_FEATURE:view-09\n");
    }

    // Клавиши эталона 6.8: страница, первая/последняя, масштаб, подгонки, поворот вида.
    void keyboardNavigationAndZoom() {
        PageView v;
        v.resize(800, 600);
        v.setPageSizes(a4Pages(10));
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.setFocus();
        QTest::keyClick(&v, Qt::Key_PageDown);
        QCOMPARE(v.currentPage(), 1);
        QTest::keyClick(&v, Qt::Key_Right);
        QCOMPARE(v.currentPage(), 2);
        QTest::keyClick(&v, Qt::Key_Left);
        QCOMPARE(v.currentPage(), 1);
        QTest::keyClick(&v, Qt::Key_End);
        QCOMPARE(v.currentPage(), 9);
        QTest::keyClick(&v, Qt::Key_PageUp);
        QCOMPARE(v.currentPage(), 8);
        QTest::keyClick(&v, Qt::Key_Home);
        QCOMPARE(v.currentPage(), 0);
        const int y = v.verticalScrollBar()->value();
        QTest::keyClick(&v, Qt::Key_Down);
        QVERIFY(v.verticalScrollBar()->value() > y);

        QTest::keyClick(&v, Qt::Key_1, Qt::ControlModifier);
        QCOMPARE(qRound(v.zoomPercent()), 100);
        QTest::keyClick(&v, Qt::Key_Equal, Qt::ControlModifier);
        QCOMPARE(qRound(v.zoomPercent()), 125);
        QTest::keyClick(&v, Qt::Key_Minus, Qt::ControlModifier);
        QCOMPARE(qRound(v.zoomPercent()), 100);
        QTest::keyClick(&v, Qt::Key_2, Qt::ControlModifier);
        QCOMPARE(v.fitMode(), std::optional<FitMode>(FitMode::Width));
        QTest::keyClick(&v, Qt::Key_0, Qt::ControlModifier);
        QCOMPARE(v.fitMode(), std::optional<FitMode>(FitMode::Page));
        QTest::keyClick(&v, Qt::Key_3, Qt::ControlModifier);
        QCOMPARE(v.fitMode(), std::optional<FitMode>(FitMode::Visible));

        // Поворот вида: документ не меняется, раскладка поворачивается.
        const QSizeF before = v.pageLayout().displaySize(0);
        QTest::keyClick(&v, Qt::Key_Equal, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(v.viewRotation(), 90);
        const QSizeF after =
            v.pageLayout().displaySize(0);  // подгонка «липкая» — масштаб другой, стороны — местами
        QVERIFY(std::fabs(after.width() / after.height() - before.height() / before.width()) < 0.01);
        QCOMPARE(v.pageSizePoints(0), QSizeF(595.2756, 841.8898));
        QTest::keyClick(&v, Qt::Key_Minus, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::keyClick(&v, Qt::Key_Underscore, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(v.viewRotation(), 270);
        v.rotateView(90);
        QCOMPARE(v.viewRotation(), 0);
        std::printf("OT_FEATURE:view-07\n");
        std::printf("OT_FEATURE:view-08\n");
    }

    // Постраничные режимы: виден один ряд, PgDn и колесо у края листают ряды.
    void pagedModesShowOneRow() {
        PageView v;
        v.resize(800, 600);
        v.setPageSizes(a4Pages(7));
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.setZoomPercent(100);
        v.setPageMode(PageMode::SinglePage);
        QCOMPARE(v.currentRow(), 0);
        QVERIFY(std::fabs(v.pageLayout().contentSize(0).height() - (842 * 96.0 / 72 + 16)) < 2.0);
        QVERIFY(v.pageViewportRect(1).isNull());
        QTest::keyClick(&v, Qt::Key_PageDown);
        QCOMPARE(v.currentPage(), 1);
        QCOMPARE(v.currentRow(), 1);
        QVERIFY(v.pageViewportRect(0).isNull());
        QVERIFY(!v.pageViewportRect(1).isNull());
        // Колесо вниз у нижнего края — следующая страница.
        v.verticalScrollBar()->setValue(v.verticalScrollBar()->maximum());
        wheel(v, QPointF(400, 300), -120);
        QCOMPARE(v.currentRow(), 2);
        QCOMPARE(v.verticalScrollBar()->value(), 0);
        wheel(v, QPointF(400, 300), 120);
        QCOMPARE(v.currentRow(), 1);

        v.setPageMode(PageMode::Facing);
        v.setCoverPage(true);
        v.goToPage(0);
        QCOMPARE(v.pageLayout().rows()[0].count, 1);
        QTest::keyClick(&v, Qt::Key_PageDown);
        QCOMPARE(v.currentPage(), 1);
        QVERIFY(!v.pageViewportRect(1).isNull());
        QVERIFY(!v.pageViewportRect(2).isNull());
        QTest::keyClick(&v, Qt::Key_PageDown);
        QCOMPARE(v.currentPage(), 3);
        std::printf("OT_FEATURE:view-06\n");
    }

    void autoScrollMovesAndStops() {
        PageView v;
        v.resize(800, 600);
        v.setPageSizes(a4Pages(5));
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.setFocus();
        QSignalSpy changed(&v, &PageView::autoScrollChanged);
        v.startAutoScroll(800);
        QVERIFY(v.isAutoScrolling());
        QTRY_VERIFY_WITH_TIMEOUT(v.verticalScrollBar()->value() > 20, 3000);
        const double speed = v.autoScrollSpeed();
        QTest::keyClick(&v, Qt::Key_Down);
        QVERIFY(v.autoScrollSpeed() > speed);
        QTest::keyClick(&v, Qt::Key_Up);
        QTest::keyClick(&v, Qt::Key_Up);
        QVERIFY(v.autoScrollSpeed() < speed);
        QTest::keyClick(&v, Qt::Key_Escape);
        QVERIFY(!v.isAutoScrolling());
        QCOMPARE(changed.count(), 2);
        // Конец документа — остановка сама.
        v.verticalScrollBar()->setValue(v.verticalScrollBar()->maximum() - 5);
        v.startAutoScroll(2000);
        QTRY_VERIFY_WITH_TIMEOUT(!v.isAutoScrolling(), 3000);
    }

    // Документ: размеры страниц из сессии (/Rotate 90 и 100 → 90), /PageLayout TwoColumnRight — разворот с
    // титулом; без «как в документе» — непрерывно.
    void documentLayoutFromFile() {
        const QString path = QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/view/layout-mixed.pdf");
        ot::DocumentSession session;
        QVERIFY(session.open(path));
        PageView v;
        v.resize(1000, 700);
        v.setDocument(&session);
        QCOMPARE(v.pageCount(), 5);
        QCOMPARE(v.pageMode(), PageMode::FacingContinuous);
        QVERIFY(v.coverPage());
        QVERIFY(std::fabs(v.pageSizePoints(1).width() - 841.89) < 0.1);
        QVERIFY(std::fabs(v.pageSizePoints(4).width() - 792) < 0.1);
        QVERIFY(std::fabs(v.pageSizePoints(4).height() - 612) < 0.1);
        QCOMPARE(v.pageLayout().rows()[0].count, 1);

        PageView plain;
        plain.setRespectDocumentLayout(false);
        plain.setDocument(&session);
        QCOMPARE(plain.pageMode(), PageMode::Continuous);
        session.close();
        QCOMPARE(v.pageCount(), 0);
    }
};

OT_UI_TEST(PageViewLayoutTest, "page_view_layout");
#include "page_view_layout_test.moc"
