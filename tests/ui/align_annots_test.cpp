// ui.align_annots — выравнивание и распределение пометок и экспорт измерений в CSV (план 5.5, п. 3;
// эталон 3.4 «Выделение и правка на странице», «Панель «Комментарии»»; comment-44, comment-40): три записки,
// выбранные «Выбором объектов», по левому краю получают равный x одним шагом «Align annotations» (отмена
// возвращает места), по центру по вертикали — равный центр; распределение по вертикали — равные промежутки,
// крайние на месте; разметка текста и «Заблокировано» не двигаются, меню «Выровнять» серое для одной пометки
// и «Распределить» — для двух; «…» панели «Комментарии» → «Экспорт измерений в CSV…» пишет измерения плана
// этажа с разделителем и десятичным знаком локали.
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>

#include "core/annot/align.h"
#include "core/annot/annots.h"
#include "core/util/features.h"
#include "services/annot_ops.h"
#include "services/app_info.h"
#include "services/document_session.h"
#include "support/ui_test.h"
#include "ui/panes/comments_pane.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/nav_pane.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

using ot::RectF;
using ot::annot::AnnotInfo;
using ot::annot::AnnotProps;
using ot::annot::AnnotRef;
using ot::annot::AnnotType;
using ot::ui::AnnotSelectTool;
using ot::ui::CommentsPane;
using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::ViewToolId;

namespace {

QString fixture(const char* rel) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/") + QLatin1String(rel);
}
QString dataDir() { return QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-align-annots"); }

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
        doc->view().setZoomPercent(50);
        doc->view().goToPage(0);
    }();
    return doc;
}

AnnotRef note(DocumentView& doc, float x, float y) {
    AnnotProps p;
    p.type = AnnotType::Text;
    p.rect = {x, y, x + 20, y + 20};
    p.contents = "note";
    return ot::annot::create(doc.session(), 0, p).result();
}

AnnotInfo infoOf(DocumentView& doc, const AnnotRef& ref) {
    for (const AnnotInfo& a : ot::annot::list(doc.session(), 0).result()) {
        if (a.ref == ref) {
            return a;
        }
    }
    return {};
}

std::vector<AnnotInfo> infos(DocumentView& doc, const std::vector<AnnotRef>& refs) {
    std::vector<AnnotInfo> out;
    for (const AnnotRef& r : refs) {
        out.push_back(infoOf(doc, r));
    }
    return out;
}

}  // namespace

class AlignAnnotsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(dataDir());
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void coreAlignAndDistribute() {
        const std::vector<RectF> rects = {{100, 100, 120, 120}, {150, 220, 190, 240}, {80, 400, 100, 430}};
        const auto left = ot::annot::alignRects(rects, ot::annot::Align::Left);
        for (const RectF& r : left) {
            QCOMPARE(r.x0, 80.0f);
        }
        QCOMPARE(left[1].x1 - left[1].x0, 40.0f);  // размер прежний
        const auto right = ot::annot::alignRects(rects, ot::annot::Align::Right);
        for (const RectF& r : right) {
            QCOMPARE(r.x1, 190.0f);
        }
        const auto middle = ot::annot::alignRects(rects, ot::annot::Align::VerticalCenter);
        for (const RectF& r : middle) {
            QCOMPARE((r.y0 + r.y1) / 2, 265.0f);
        }
        const auto spread = ot::annot::distributeRects(rects, ot::annot::Distribute::Vertical);
        QCOMPARE(spread[0].y0, 100.0f);
        QCOMPARE(spread[2].y1, 430.0f);
        QVERIFY(std::abs((spread[1].y0 - spread[0].y1) - (spread[2].y0 - spread[1].y1)) < 1e-3f);
        QCOMPARE(ot::annot::alignRects({rects[0]}, ot::annot::Align::Left).front().x0, 100.0f);
    }

    void threeNotesAlignLeft() {
        OT_FEATURE("comment-44");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "annots/measure-viewport.pdf");
        QVERIFY(doc != nullptr);
        const std::vector<AnnotRef> refs = {note(*doc, 100, 600), note(*doc, 160, 650), note(*doc, 130, 720)};
        const std::vector<AnnotInfo> before = infos(*doc, refs);
        const float left = std::min({before[0].rect.x0, before[1].rect.x0, before[2].rect.x0});
        AnnotSelectTool& select = doc->view().tools().annotSelect();
        doc->view().tools().setTool(ViewToolId::AnnotSelect);
        select.setSelection(infos(*doc, refs));
        QCOMPARE(select.alignable().size(), std::size_t{3});
        QMenu menu;
        select.fillContextMenu(menu);
        auto* alignMenu = menu.findChild<QMenu*>(QStringLiteral("annotAlignMenu"));
        QVERIFY(alignMenu != nullptr);
        QVERIFY(alignMenu->isEnabled());
        QCOMPARE(alignMenu->actions().size(), 6);
        QCOMPARE(alignMenu->actions().front()->text(), QStringLiteral("По левому краю"));
        auto* distributeMenu = menu.findChild<QMenu*>(QStringLiteral("annotDistributeMenu"));
        QVERIFY(distributeMenu != nullptr && distributeMenu->isEnabled());

        alignMenu->actions().front()->trigger();
        QTRY_VERIFY(std::abs(infoOf(*doc, refs[1]).rect.x0 - left) < 0.01f);
        for (const AnnotRef& r : refs) {
            QVERIFY2(std::abs(infoOf(*doc, r).rect.x0 - left) < 0.01f, qPrintable(QString::number(r.objnum)));
        }
        QCOMPARE(infoOf(*doc, refs[2]).rect.y0, before[2].rect.y0);  // по вертикали — на месте
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Align annotations"));
        // Одна отмена — все на прежних местах.
        doc->session().undo().waitForFinished();
        QTRY_VERIFY(std::abs(infoOf(*doc, refs[1]).rect.x0 - before[1].rect.x0) < 0.01f);
        QVERIFY(std::abs(infoOf(*doc, refs[2]).rect.x0 - before[2].rect.x0) < 0.01f);

        // Распределить по вертикали: промежутки равны, крайние на месте.
        select.setSelection(infos(*doc, refs));
        select.distributeSelection(ot::annot::Distribute::Vertical);
        QTRY_COMPARE(doc->session().undoState().undoName, QStringLiteral("Distribute annotations"));
        const RectF a = infoOf(*doc, refs[0]).rect;
        const RectF b = infoOf(*doc, refs[1]).rect;
        const RectF c = infoOf(*doc, refs[2]).rect;
        QCOMPARE(a.y0, before[0].rect.y0);
        QCOMPARE(c.y0, before[2].rect.y0);
        QVERIFY(std::abs((b.y0 - a.y1) - (c.y0 - b.y1)) < 0.05f);
    }

    void lockedAndSingleAreNotAligned() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "annots/measure-viewport.pdf");
        QVERIFY(doc != nullptr);
        const AnnotRef a = note(*doc, 100, 600);
        const AnnotRef b = note(*doc, 160, 650);
        ot::annot::setFlags(doc->session(), {b}, ot::annot::kAnnotLocked, 0).waitForFinished();
        const float bx = infoOf(*doc, b).rect.x0;
        AnnotSelectTool& select = doc->view().tools().annotSelect();
        doc->view().tools().setTool(ViewToolId::AnnotSelect);
        select.setSelection(infos(*doc, {a, b}));
        QCOMPARE(select.alignable().size(), std::size_t{1});
        QMenu menu;
        select.fillContextMenu(menu);
        auto* alignMenu = menu.findChild<QMenu*>(QStringLiteral("annotAlignMenu"));
        QVERIFY(alignMenu != nullptr);
        QVERIFY(!alignMenu->isEnabled());
        select.alignSelection(ot::annot::Align::Left);
        QTest::qWait(100);
        QCOMPARE(infoOf(*doc, b).rect.x0, bx);
    }

    void exportMeasuresCsv() {
        OT_FEATURE("comment-40");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "annots/measure-viewport.pdf");
        QVERIFY(doc != nullptr);
        w.navPane().showPane(QStringLiteral("comments"));
        auto* pane = qobject_cast<CommentsPane*>(w.navPane().page(QStringLiteral("comments")));
        QVERIFY(pane != nullptr);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("plan.csv"));
        pane->setSaveChooser([&](const QString& suggested, const QString& filter) {
            [&] {
                QVERIFY(suggested.endsWith(QStringLiteral(".csv")));
                QVERIFY(filter.contains(QStringLiteral("*.csv")));
            }();
            return path;
        });
        QSignalSpy exported(pane, &CommentsPane::measuresExported);
        pane->fillMoreMenu();
        auto* action = pane->moreMenu()->findChild<QAction*>(QStringLiteral("commentsExportMeasures"));
        QVERIFY(action != nullptr);
        QVERIFY(action->isEnabled());
        action->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(exported.count(), 1, 10000);
        QCOMPARE(exported.front().at(1).toInt(), 2);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QVERIFY(bytes.startsWith("\xEF\xBB\xBF"));
        const QStringList lines = QString::fromUtf8(bytes.mid(3)).split(QStringLiteral("\r\n"));
        QVERIFY(lines.size() >= 3);
        const QString sep =
            QLocale().decimalPoint() == QLatin1String(",") ? QStringLiteral(";") : QStringLiteral(",");
        QCOMPARE(lines[0],
                 QStringList({QStringLiteral("Страница"), QStringLiteral("Тип"), QStringLiteral("Значение"),
                              QStringLiteral("Единицы"), QStringLiteral("Подпись")})
                     .join(sep));
        const QString value = QStringLiteral("15") + QLocale().decimalPoint() + QStringLiteral("52");
        QVERIFY2(lines[1].startsWith(QStringLiteral("1") + sep + QStringLiteral("Расстояние") + sep + value +
                                     sep + QStringLiteral("м") + sep),
                 qPrintable(lines[1]));
        QVERIFY2(lines[2].contains(QStringLiteral("Площадь") + sep), qPrintable(lines[2]));
        QVERIFY2(lines[2].contains(sep + QStringLiteral("м²") + sep), qPrintable(lines[2]));
    }
};

OT_UI_TEST(AlignAnnotsTest, "align_annots");
#include "align_annots_test.moc"
