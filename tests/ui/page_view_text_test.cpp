// ui.page_view_text — текст и ссылки в виджете (план 2.2b; эталон 3.1 «Навигация», 3.2, 6.9; view-13,
// view-18, ocr-41): выделение мышью двух строк search.pdf и Ctrl+C — text/plain и text/html в буфере;
// клавиатурный курсор — щелчок ставит, Shift+←/→/↑/↓ ведут выделение; совпадения поиска — подсветка всех и
// текущее с прокруткой; ссылки links.pdf — курсор-указатель, щелчок по GoTo переходит (Alt+← — назад), /Named
// — на следующую страницу, /Launch и /URI не переходят (только сигнал), протягивание через ссылку — не
// переход; Tab/Shift+Tab — фокус по ссылкам, Enter — переход.
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QScrollBar>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "core/text/search.h"
#include "core/text/stext.h"
#include "services/document_session.h"
#include "services/link_ops.h"
#include "support/ui_test.h"
#include "ui/view/link_layer.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"
#include "ui/view/tool_controller.h"
#include "ui/view/tools/view_tool.h"

using ot::links::LinkAction;
using ot::links::LinkInfo;
using ot::links::LinkKind;
using ot::ui::PageView;
using ot::ui::ToolController;
using ot::ui::ViewToolId;

namespace {

QString fixture(const char* rel) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/") + QLatin1String(rel);
}

struct Opened {
    ot::DocumentSession session;
    PageView view;
};

void openFile(Opened& o, const QString& path) {
    QVERIFY(o.session.open(path));
    o.view.resize(1000, 800);
    o.view.setDocument(&o.session);
    o.view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&o.view));
    o.view.setZoomPercent(100);
    o.view.goToPage(0);
    o.view.setFocus();
}

QPoint at(const PageView& v, int page, double x, double y) {
    return v.pageTransform(page).map(QPointF(x, y)).toPoint();
}

QPoint center(const PageView& v, const LinkInfo& link) {
    return at(v, link.page, (link.rect.x0 + link.rect.x1) / 2, (link.rect.y0 + link.rect.y1) / 2);
}

void drag(PageView& v, QPoint from, QPoint to) {
    QTest::mousePress(v.viewport(), Qt::LeftButton, {}, from);
    QTest::mouseMove(v.viewport(), (from + to) / 2);
    QTest::mouseMove(v.viewport(), to);
    QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, to);
}

// Щелчок, после которого следующий не станет двойным.
void click(PageView& v, QPoint p, Qt::MouseButton button = Qt::LeftButton) {
    QTest::mouseClick(v.viewport(), button, {}, p);
    QTest::qWait(QApplication::doubleClickInterval() + 30);
}

// Снимок вида — build/<пресет>/ui-shots/<имя>.png (смотреть глазами).
void saveShot(PageView& w, const QString& name) {
    for (int i = 0; i < 250 && !w.tilePainter().visibleComplete(); ++i) {
        w.viewport()->repaint();
        QTest::qWait(20);
    }
    const QString dir = QStringLiteral(OTTISK_BINARY_DIR "/ui-shots");
    QDir().mkpath(dir);
    w.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
}

LinkAction lastAction(const QSignalSpy& spy) { return spy.last().first().value<LinkAction>(); }

}  // namespace

class PageViewTextTest : public QObject {
    Q_OBJECT
private slots:

    void copyTwoLinesAsTextAndHtml() {
        Opened o;
        openFile(o, fixture("text/search.pdf"));
        PageView& v = o.view;
        auto& select = v.tools().select();
        QTest::mouseMove(v.viewport(), at(v, 0, 150, 257));
        QTRY_VERIFY_WITH_TIMEOUT(select.hasTextPage(0), 10000);
        drag(v, at(v, 0, 72.5, 257), at(v, 0, 250, 274));
        const QString expected = QStringLiteral("Первая строка для выделения.\nВторая строка для выделения.");
        QCOMPARE(select.selectedText(), expected);
        QApplication::clipboard()->clear();
        QTest::keyClick(&v, Qt::Key_C, Qt::ControlModifier);
        const QMimeData* mime = QApplication::clipboard()->mimeData();
        QVERIFY(mime != nullptr);
        QCOMPARE(mime->text(), expected);
        QVERIFY(mime->hasHtml());
        const QString html = mime->html();
        QVERIFY2(html.startsWith(QStringLiteral("<html><head><meta charset=\"utf-8\"></head><body>")),
                 qPrintable(html));
        QVERIFY2(html.contains(QStringLiteral("Первая строка для выделения.")), qPrintable(html));
        QVERIFY2(html.contains(QStringLiteral("Вторая строка для выделения.")), qPrintable(html));
        QVERIFY2(html.contains(QStringLiteral("<br>")) || html.contains(QStringLiteral("</p><p>")),
                 qPrintable(html));
        QVERIFY2(!html.contains(QStringLiteral("Жирная")), qPrintable(html));
        // Без выделения Ctrl+C ничего не кладёт.
        QTest::keyClick(&v, Qt::Key_Escape);
        QApplication::clipboard()->setText(QStringLiteral("прежнее"));
        QTest::keyClick(&v, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("прежнее"));
        std::printf("OT_FEATURE:view-33\n");
    }

    // ocr-41: щелчок ставит курсор, Shift+стрелки ведут выделение; курсор виден.
    void keyboardCaretSelects() {
        Opened o;
        openFile(o, fixture("text/search.pdf"));
        PageView& v = o.view;
        auto& select = v.tools().select();
        QTest::mouseMove(v.viewport(), at(v, 0, 160, 258));
        QTRY_VERIFY_WITH_TIMEOUT(select.hasTextPage(0), 10000);
        click(v, at(v, 0, 71.5, 257));  // перед «Первая»
        QVERIFY(select.caret().valid());
        QCOMPARE(select.caret().page, 0);
        QVERIFY(select.caretRect().has_value());
        QVERIFY(!select.hasSelection());
        const int scroll = v.verticalScrollBar()->value();
        for (int i = 0; i < 6; ++i) {
            QTest::keyClick(&v, Qt::Key_Right, Qt::ShiftModifier);
        }
        QCOMPARE(select.selectedText(), QStringLiteral("Первая"));
        QCOMPARE(v.verticalScrollBar()->value(), scroll);  // не прокрутка
        QCOMPARE(v.currentPage(), 0);                      // и не листание
        QTest::keyClick(&v, Qt::Key_Left, Qt::ShiftModifier);
        QCOMPARE(select.selectedText(), QStringLiteral("Перва"));
        QTest::keyClick(&v, Qt::Key_Right, Qt::ShiftModifier);
        // Строкой ниже — до той же x.
        QTest::keyClick(&v, Qt::Key_Down, Qt::ShiftModifier);
        const QString two = select.selectedText();
        QVERIFY2(two.startsWith(QStringLiteral("Первая строка для выделения.\nВтор")), qPrintable(two));
        QVERIFY2(two.length() <= QStringLiteral("Первая строка для выделения.\nВторая ").length(),
                 qPrintable(two));
        saveShot(v, QStringLiteral("page_view_text_caret"));
        QTest::keyClick(&v, Qt::Key_Up, Qt::ShiftModifier);
        QCOMPARE(select.selectedText(), QStringLiteral("Первая"));
        // Курсор рисуется (слой инструмента) — снимок для глаза.
        v.repaint();
        QVERIFY(select.caretRect().has_value());
        // Мышь сбрасывает выделение клавишами.
        click(v, at(v, 0, 300, 400));
        QVERIFY(!select.hasSelection());
        std::printf("OT_FEATURE:ocr-41\n");
    }

    // view-18: подсветка всех совпадений, текущее — обводка и прокрутка к нему.
    void searchHitsHighlight() {
        Opened o;
        openFile(o, fixture("text/search.pdf"));
        PageView& v = o.view;
        std::vector<ot::SearchHit> hits;
        for (int page = 0; page < v.pageCount(); ++page) {
            const auto text = o.session.textPage(page, ot::SearchSession::textOptions()).result();
            QVERIFY(text != nullptr);
            for (ot::SearchHit& hit : ot::searchPage(*text, "оттиск")) {
                hits.push_back(std::move(hit));
            }
        }
        QVERIFY(hits.size() >= 4);
        int onFirst = 0;
        int firstOnSecond = -1;
        for (int i = 0; i < static_cast<int>(hits.size()); ++i) {
            if (hits[static_cast<std::size_t>(i)].page == 0) {
                onFirst += static_cast<int>(hits[static_cast<std::size_t>(i)].quads.size());
            } else if (firstOnSecond < 0 && hits[static_cast<std::size_t>(i)].page == 1) {
                firstOnSecond = i;
            }
        }
        QVERIFY(firstOnSecond > 0);
        v.setSearchHits(hits);
        QCOMPARE(static_cast<int>(v.searchOverlay().quads(0).size()), onFirst);
        QVERIFY(!v.searchOverlay().quads(1).empty());
        QVERIFY(v.searchOverlay().current().empty());
        v.setCurrentSearchHit(firstOnSecond);
        QCOMPARE(v.currentSearchHit(), firstOnSecond);
        QCOMPARE(v.currentPage(), 1);
        QCOMPARE(static_cast<int>(v.searchOverlay().current().size()),
                 static_cast<int>(hits[static_cast<std::size_t>(firstOnSecond)].quads.size()));
        const ot::QuadF q = hits[static_cast<std::size_t>(firstOnSecond)].quads.front().quad;
        const QRectF onScreen =
            v.pageTransform(1).mapRect(QRectF(QPointF(q.ul.x, q.ul.y), QPointF(q.lr.x, q.lr.y)));
        QVERIFY2(QRectF(v.viewport()->rect()).contains(onScreen),
                 qPrintable(QStringLiteral("%1,%2").arg(onScreen.x()).arg(onScreen.y())));
        v.clearSearchHits();
        QVERIFY(v.searchOverlay().empty());
        QCOMPARE(v.currentSearchHit(), -1);
        std::printf("OT_FEATURE:view-18\n");
    }

    // view-13: щелчок по ссылкам разных видов.
    void linksFollowOnClick() {
        Opened o;
        openFile(o, fixture("view/links.pdf"));
        PageView& v = o.view;
        const std::vector<LinkInfo> links = ot::links::list(o.session, 0).result();
        QCOMPARE(static_cast<int>(links.size()), 7);
        QTRY_VERIFY_WITH_TIMEOUT(v.linkLayer().hasLinks(0), 10000);  // заранее — при отрисовке
        // Над ссылкой — курсор-указатель (и у «Выделения», и у «Руки»), мимо — нет.
        QTest::mouseMove(v.viewport(), center(v, links[0]));
        QCOMPARE(v.viewport()->cursor().shape(), Qt::PointingHandCursor);
        QTest::mouseMove(v.viewport(), at(v, 0, 500, 400));
        QVERIFY(v.viewport()->cursor().shape() != Qt::PointingHandCursor);

        QSignalSpy activated(&v, &PageView::linkActivated);
        // GoTo: страница 3, /XYZ — точка назначения в левом верхнем углу; Alt+← — назад.
        click(v, center(v, links[0]));
        QCOMPARE(activated.count(), 1);
        QCOMPARE(lastAction(activated).kind, LinkKind::GoTo);
        QCOMPARE(v.currentPage(), 2);
        const QPointF dest = v.pageTransform(2).map(QPointF(72, links[0].action.point->y));
        QVERIFY2(std::abs(dest.y()) <= 1.0, qPrintable(QString::number(dest.y())));
        QTest::keyClick(&v, Qt::Key_Left, Qt::AltModifier);
        QCOMPARE(v.currentPage(), 0);
        // /Launch — сигнал есть (окно скажет «запрещено»), перехода и запуска нет.
        QTRY_VERIFY_WITH_TIMEOUT(v.linkLayer().hasLinks(0), 10000);
        const int scroll = v.verticalScrollBar()->value();
        click(v, center(v, links[5]));
        QCOMPARE(activated.count(), 2);
        QCOMPARE(lastAction(activated).kind, LinkKind::Launch);
        QCOMPARE(lastAction(activated).file, std::string("calc.exe"));
        QCOMPARE(v.currentPage(), 0);
        QCOMPARE(v.verticalScrollBar()->value(), scroll);
        // /URI — только сигнал (подтверждение — окно, эталон 7.4).
        click(v, center(v, links[3]));
        QCOMPARE(lastAction(activated).kind, LinkKind::Uri);
        QCOMPARE(v.currentPage(), 0);
        // Протянули через ссылку — это выделение, не переход.
        drag(v, center(v, links[0]) - QPoint(60, 0), center(v, links[0]) + QPoint(60, 0));
        QCOMPARE(activated.count(), 3);
        QCOMPARE(v.currentPage(), 0);
        // «Рука» тоже ходит по ссылкам: /Named /NextPage.
        v.tools().setTool(ViewToolId::Hand);
        QTest::mouseMove(v.viewport(), center(v, links[2]) + QPoint(1, 0));
        QCOMPARE(v.viewport()->cursor().shape(), Qt::PointingHandCursor);
        click(v, center(v, links[2]));
        QCOMPARE(lastAction(activated).kind, LinkKind::Named);
        QCOMPARE(v.currentPage(), 1);
        // «Увеличение области» ссылками не пользуется.
        v.goToPage(0);
        v.tools().setTool(ViewToolId::MarqueeZoom);
        QTest::mouseMove(v.viewport(), center(v, links[0]) + QPoint(2, 0));
        QVERIFY(v.viewport()->cursor().shape() != Qt::PointingHandCursor);
        std::printf("OT_FEATURE:view-13\n");
    }

    // Tab/Shift+Tab — фокус по ссылкам видимых страниц, Enter — переход (эталон 6.9).
    void tabFocusesLinks() {
        Opened o;
        openFile(o, fixture("view/links.pdf"));
        PageView& v = o.view;
        const std::vector<LinkInfo> links = ot::links::list(o.session, 0).result();
        QTRY_VERIFY_WITH_TIMEOUT(v.linkLayer().hasLinks(0), 10000);
        QVERIFY(v.linkLayer().focused() == nullptr);
        QTest::keyClick(&v, Qt::Key_Tab);
        QVERIFY(v.linkLayer().focused() != nullptr);
        QCOMPARE(v.linkLayer().focused()->objnum, links[0].objnum);
        QTest::keyClick(&v, Qt::Key_Tab);
        QCOMPARE(v.linkLayer().focused()->objnum, links[1].objnum);
        QTest::keyClick(&v, Qt::Key_Backtab, Qt::ShiftModifier);
        QCOMPARE(v.linkLayer().focused()->objnum, links[0].objnum);
        QVERIFY(v.hasFocus());
        saveShot(v, QStringLiteral("page_view_text_link_focus"));  // рамка фокуса рисуется слоем
        QSignalSpy activated(&v, &PageView::linkActivated);
        QTest::keyClick(&v, Qt::Key_Tab);
        QTest::keyClick(&v, Qt::Key_Return);  // «К разделу «Итоги»» — именованное назначение
        QCOMPARE(activated.count(), 1);
        QCOMPARE(lastAction(activated).destName, std::string("itogi"));
        QCOMPARE(v.currentPage(), 2);
        // Назад и до конца: после последней ссылки фокус уходит со ссылок.
        QTest::keyClick(&v, Qt::Key_Left, Qt::AltModifier);
        QCOMPARE(v.currentPage(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(v.linkLayer().hasLinks(0), 10000);
        v.linkLayer().clearFocus();
        for (int i = 0; i < 7; ++i) {
            QTest::keyClick(&v, Qt::Key_Tab);
        }
        QCOMPARE(v.linkLayer().focused()->objnum, links[6].objnum);
        QTest::keyClick(&v, Qt::Key_Tab);
        QVERIFY(v.linkLayer().focused() == nullptr);
        // Щелчок мышью снимает фокус ссылки.
        QTest::keyClick(&v, Qt::Key_Tab);
        QVERIFY(v.linkLayer().focused() != nullptr);
        click(v, at(v, 0, 500, 420));
        QVERIFY(v.linkLayer().focused() == nullptr);
        std::printf("OT_FEATURE:view-13\n");
    }
};

OT_UI_TEST(PageViewTextTest, "page_view_text");
#include "page_view_text_test.moc"
