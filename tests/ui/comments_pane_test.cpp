// ui.comments_pane — панель «Комментарии» (план 5.4, п. 2; эталон 3.4 «Панель «Комментарии»», 6.1, 6.8
// Ctrl+8; comment-18, comment-19): карточки 16 комментариев annots-all.pdf (без Popup и служебных); ответы —
// детьми своей карточки; сортировка по каждому ключу меняет порядок; фильтр по автору, типу и статусу и поиск
// меняют список и страницу (скрытые не рисуются — кадр другой, ключ тайлов несёт отпечаток; не выбираются),
// Ctrl+8 сбрасывает всё; щелчок по карточке — переход и выделение на странице, выделение на странице —
// карточка; чужой комментарий непрочитан до щелчка; удаление с ответами — подтверждение и ветка целиком
// одним шагом (undo возвращает); 1 500 комментариев — список виртуальный (строки одной высоты).
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeView>
#include <QtTest/QtTest>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "core/annot/annots.h"
#include "core/annot/comments.h"
#include "core/annot/defaults.h"
#include "core/doc/document.h"
#include "core/doc/saver.h"
#include "core/util/features.h"
#include "services/annot_ops.h"
#include "services/app_info.h"
#include "services/commands.h"
#include "services/comments_ops.h"
#include "services/document_session.h"
#include "support/ui_test.h"
#include "ui/panes/comments_pane.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/nav_pane.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/empty_state.h"
#include "ui/widgets/search_field.h"
#include "ui/widgets/tool_button.h"

using ot::annot::AnnotProps;
using ot::annot::AnnotRef;
using ot::annot::AnnotType;
using ot::annot::CommentFilter;
using ot::annot::CommentSort;
using ot::annot::ReviewState;
using ot::ui::CommentsPane;
using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::PageView;

namespace {

DocumentView* open(MainWindow& w, const QString& rel) {
    DocumentView* doc = nullptr;
    [&] {
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        w.resize(1440, 900);
        w.show();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/") + rel));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        doc = w.currentDocument();
        doc->view().setZoomPercent(100);
        doc->view().goToPage(0);
    }();
    return doc;
}

CommentsPane* showPane(MainWindow& w) {
    w.navPane().showPane(QStringLiteral("comments"));
    auto* pane = qobject_cast<CommentsPane*>(w.navPane().page(QStringLiteral("comments")));
    return pane;
}

QImage frame(PageView& v) {
    QImage image;
    [&] {
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                v.viewport()->repaint();
                return v.tilePainter().visibleComplete();
            }(),
            15000);
        image = v.viewport()->grab().toImage();
    }();
    return image;
}

AnnotProps note(float x, float y, const char* author, const char* text, std::int64_t when) {
    AnnotProps p = ot::annot::toolDefaults(AnnotType::Text);
    p.rect = {x, y, x + 20, y + 20};
    p.author = author;
    p.contents = text;
    p.created = when;
    p.modified = when;
    return p;
}

AnnotProps square(float x, float y, const char* author, const char* text, std::int64_t when) {
    AnnotProps p = ot::annot::toolDefaults(AnnotType::Square);
    p.rect = {x, y, x + 80, y + 50};
    p.author = author;
    p.contents = text;
    p.created = when;
    p.modified = when;
    p.color = ot::annot::AnnotColor::rgb(1, 0, 0);
    return p;
}

std::vector<std::string> shownTexts(const CommentsPane& pane) {
    std::vector<std::string> out;
    for (const AnnotRef& ref : pane.shownRoots()) {
        out.push_back(pane.comment(ref)->contents);
    }
    return out;
}

// Пять комментариев на двух страницах отчёта, статусы и ответ.
struct Review {
    AnnotRef a, b, c, d, e;
};

Review makeReview(DocumentView& doc) {
    ot::DocumentSession& s = doc.session();
    Review r;
    r.a = ot::annot::create(s, 1, note(60, 80, "Борис", "альфа", 1'700'000'300)).result();
    r.b = ot::annot::create(s, 0, square(100, 300, "Анна", "Бета проверка", 1'700'000'100)).result();
    r.c = ot::annot::create(s, 0, note(60, 520, "Вера", "гамма", 1'700'000'200)).result();
    r.d = ot::annot::create(s, 1, square(220, 300, "Анна", "дельта", 1'700'000'500)).result();
    r.e = ot::annot::create(s, 0, note(320, 80, "Борис", "эпсилон", 1'700'000'400)).result();
    ot::comments::setStatus(s, r.b, std::string(ot::annot::kReviewModel), "Accepted", "Вера")
        .waitForFinished();
    ot::comments::setStatus(s, r.d, std::string(ot::annot::kReviewModel), "Rejected", "Борис")
        .waitForFinished();
    ot::comments::setStatus(s, r.e, std::string(ot::annot::kReviewModel), "Completed", "Анна")
        .waitForFinished();
    ot::comments::reply(s, r.b, {.author = "Вера", .contents = "Ответ на бету"}).waitForFinished();
    return r;
}

bool waitLoaded(CommentsPane* pane, int roots) {
    return QTest::qWaitFor(
        [&] { return pane->isLoaded() && static_cast<int>(pane->tree().roots.size()) == roots; }, 10000);
}

}  // namespace

class CommentsPaneTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-comments_pane"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void cardsOfMatrix() {
        OT_FEATURE("comment-18");
        MainWindow w;
        DocumentView* doc = open(w, QStringLiteral("annots/annots-all.pdf"));
        QVERIFY(doc != nullptr);
        CommentsPane* pane = showPane(w);
        QVERIFY(pane != nullptr);
        QVERIFY(waitLoaded(pane, 16));
        QCOMPARE(pane->shownCount(), 16);
        QCOMPARE(pane->list()->model()->rowCount(), 16);
        QVERIFY(!pane->isEmptyShown());
        QVERIFY(pane->list()->uniformRowHeights());
        // Ответ — ребёнок своей карточки.
        const AnnotRef text = pane->shownRoots().front();
        QCOMPARE(pane->comment(text)->type, AnnotType::Text);
        ot::comments::reply(doc->session(), text, {.author = "Анна", .contents = "ответ"}).waitForFinished();
        QTRY_COMPARE_WITH_TIMEOUT(pane->comment(text) != nullptr ? pane->comment(text)->replyCount() : 0, 1,
                                  10000);
        QCOMPARE(pane->list()->model()->rowCount(pane->list()->model()->index(0, 0)), 1);
        QCOMPARE(pane->shownCount(), 16);
        QVERIFY(pane->list()->isExpanded(pane->list()->model()->index(0, 0)));
    }

    void sortFilterSearchAndPage() {
        OT_FEATURE("comment-19");
        MainWindow w;
        DocumentView* doc = open(w, QStringLiteral("demo/report.pdf"));
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        CommentsPane* pane = showPane(w);
        makeReview(*doc);
        QVERIFY(waitLoaded(pane, 5));
        QCOMPARE(shownTexts(*pane),
                 (std::vector<std::string>{"Бета проверка", "гамма", "эпсилон", "альфа", "дельта"}));
        pane->setSort(CommentSort::Author);
        QCOMPARE(shownTexts(*pane),
                 (std::vector<std::string>{"Бета проверка", "дельта", "эпсилон", "альфа", "гамма"}));
        pane->setSort(CommentSort::Date, true);
        QCOMPARE(shownTexts(*pane),
                 (std::vector<std::string>{"дельта", "эпсилон", "альфа", "гамма", "Бета проверка"}));
        pane->setSort(CommentSort::Type);
        QCOMPARE(shownTexts(*pane),
                 (std::vector<std::string>{"гамма", "эпсилон", "альфа", "Бета проверка", "дельта"}));
        pane->setSort(CommentSort::Status);
        QCOMPARE(shownTexts(*pane),
                 (std::vector<std::string>{"гамма", "альфа", "Бета проверка", "дельта", "эпсилон"}));
        // Меню сортировки — те же ключи.
        pane->sortMenu()->clear();
        QMetaObject::invokeMethod(pane->sortMenu(), "aboutToShow");
        auto* byPage = pane->sortMenu()->findChild<QAction*>(QStringLiteral("commentsSort0"));
        QVERIFY(byPage != nullptr);
        byPage->trigger();
        QCOMPARE(pane->sort(), CommentSort::Page);

        const QImage all = frame(v);
        // Фильтр по автору: список и страница.
        CommentFilter f;
        f.authors = {"Анна"};
        pane->setFilter(f);
        QCOMPARE(shownTexts(*pane), (std::vector<std::string>{"Бета проверка", "дельта"}));
        QVERIFY(pane->filterButton()->isChecked());
        QVERIFY(pane->filterSummary()->isVisible() || !pane->isVisible());
        const auto& hidden = doc->filteredAnnots();
        QVERIFY(hidden.contains(0));
        QCOMPARE(v.tilePainter().hiddenAnnots(), hidden);
        QVERIFY(frame(v) != all);
        // Скрытый фильтром не выбирается.
        v.tools().setTool(ot::ui::ViewToolId::AnnotSelect);
        const ot::annot::Comment* gamma = nullptr;
        for (const ot::annot::Comment& c : pane->tree().roots) {
            if (c.contents == "гамма") {
                gamma = &c;
            }
        }
        QVERIFY(gamma != nullptr);
        const QPoint at =
            v.pageTransform(0)
                .map(QPointF((gamma->rect.x0 + gamma->rect.x1) / 2, (gamma->rect.y0 + gamma->rect.y1) / 2))
                .toPoint();
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at);
        QTest::qWait(300);
        QVERIFY(v.tools().annotSelect().selection().empty());

        // Тип и статус — через меню фильтра.
        pane->clearFilter();
        QVERIFY(doc->filteredAnnots().empty());
        pane->fillFilterMenu();
        QMenu* types = pane->filterMenu()->findChild<QMenu*>(QStringLiteral("commentsFilterTypes"));
        QVERIFY(types != nullptr);
        QCOMPARE(types->actions().size(), 2);  // записка и прямоугольник
        types->actions().front()->trigger();   // записка (Text — первый по порядку подтипов)
        QCOMPARE(shownTexts(*pane), (std::vector<std::string>{"гамма", "эпсилон", "альфа"}));
        pane->clearFilter();
        pane->fillFilterMenu();
        QMenu* states = pane->filterMenu()->findChild<QMenu*>(QStringLiteral("commentsFilterStates"));
        QVERIFY(states != nullptr);
        QCOMPARE(states->actions().size(), ot::annot::kReviewStateCount);
        states->actions()[1]->trigger();  // Принято
        QCOMPARE(shownTexts(*pane), (std::vector<std::string>{"Бета проверка"}));

        // Поиск (и по ответам), Ctrl+8 сбрасывает и поиск, и фильтр.
        pane->clearFilter();
        pane->setSearchText(QStringLiteral("ОТВЕТ на"));
        QCOMPARE(shownTexts(*pane), (std::vector<std::string>{"Бета проверка"}));
        QVERIFY(!doc->filteredAnnots().empty());
        f = {};
        f.states = {ReviewState::Rejected};
        pane->setFilter(f);
        QVERIFY(pane->isEmptyShown());
        QVERIFY(!pane->emptyState()->actionButton()->isHidden());
        QVERIFY(w.commands().trigger(QStringLiteral("comment.clearFilter")));
        QVERIFY(!pane->filterActive());
        QVERIFY(pane->searchField()->text().isEmpty());
        QCOMPARE(pane->shownCount(), 5);
        QVERIFY(doc->filteredAnnots().empty());
        QCOMPARE(frame(v), all);
    }

    void clickSelectsOnPageAndBack() {
        OT_FEATURE("comment-18");
        MainWindow w;
        DocumentView* doc = open(w, QStringLiteral("demo/report.pdf"));
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        CommentsPane* pane = showPane(w);
        const Review r = makeReview(*doc);
        QVERIFY(waitLoaded(pane, 5));
        const ot::annot::Comment* delta = pane->comment(r.d);
        QVERIFY(delta != nullptr);
        QVERIFY(pane->isUnread(*delta));  // чужой
        // Щелчок по карточке «дельта» (стр. 2): переход и выделение на странице, прочитано.
        const QModelIndex index = pane->list()->model()->index(4, 0);
        pane->list()->scrollTo(index);
        QTest::mouseClick(pane->list()->viewport(), Qt::LeftButton, {},
                          pane->list()->visualRect(index).center());
        QTRY_COMPARE(v.currentPage(), 1);
        QCOMPARE(static_cast<int>(v.tools().annotSelect().selection().size()), 1);
        QCOMPARE(v.tools().annotSelect().selection().front().ref, r.d);
        QVERIFY(!pane->isUnread(*pane->comment(r.d)));
        // Прочитано переживает перечтение дерева.
        ot::comments::reply(doc->session(), r.a, {.author = "Анна", .contents = "к альфе"}).waitForFinished();
        QTRY_VERIFY(pane->comment(r.a) != nullptr && pane->comment(r.a)->replyCount() == 1);
        QVERIFY(!pane->isUnread(*pane->comment(r.d)));
        QCOMPARE(pane->currentComment(), std::optional<AnnotRef>(r.d));
        // Выделение на странице — карточка.
        for (const ot::annot::AnnotInfo& info : ot::annot::list(doc->session(), 0).result()) {
            if (info.ref == r.b) {
                v.tools().annotSelect().setSelection({info});
            }
        }
        QTRY_COMPARE(pane->currentComment(), std::optional<AnnotRef>(r.b));
    }

    void removeBranchWithConfirm() {
        OT_FEATURE("comment-18");
        MainWindow w;
        DocumentView* doc = open(w, QStringLiteral("demo/report.pdf"));
        QVERIFY(doc != nullptr);
        CommentsPane* pane = showPane(w);
        const Review r = makeReview(*doc);
        QVERIFY(waitLoaded(pane, 5));
        QString asked;
        pane->setConfirm([&](const QString& text) {
            asked = text;
            return false;
        });
        QMenu menu;
        pane->fillRecordMenu(menu, r.b);
        auto* remove = menu.findChild<QAction*>(QStringLiteral("commentsRemove"));
        QVERIFY(remove != nullptr && remove->isEnabled());
        remove->trigger();
        QVERIFY(asked.contains(QLatin1Char('1')));
        QTest::qWait(100);
        QCOMPARE(static_cast<int>(pane->tree().roots.size()), 5);
        pane->setConfirm([](const QString&) { return true; });
        remove->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(pane->tree().roots.size()), 4, 10000);
        QCOMPARE(pane->comment(r.b), nullptr);
        QCOMPARE(doc->session().undoState().undoName, QString::fromLatin1(ot::comments::kDeleteCommentStep));
        // Ответ и статус ушли вместе с ним.
        for (const ot::annot::AnnotInfo& info : ot::annot::list(doc->session(), 0).result()) {
            QVERIFY(info.inReplyTo != r.b.objnum);
        }
        doc->session().undo().waitForFinished();
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(pane->tree().roots.size()), 5, 10000);
        QCOMPARE(pane->comment(r.b)->replyCount(), 1);
        // Без ответов — без вопроса.
        asked.clear();
        pane->setConfirm([&](const QString& text) {
            asked = text;
            return true;
        });
        pane->removeComment(r.c);
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(pane->tree().roots.size()), 4, 10000);
        QVERIFY(asked.isEmpty());
    }

    void manyCommentsVirtualized() {
        OT_FEATURE("comment-18");
        // 1 500 записок пишет ядро в копию отчёта (через окно по шагу — минуты).
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("many.pdf"));
        QVERIFY(QFile::copy(QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/demo/report.pdf"), path));
        {
            auto core = ot::Document::openOrThrow(std::filesystem::path(path.toStdWString()));
            for (int i = 0; i < 1500; ++i) {
                ot::annot::create(
                    *core, 0,
                    note(20.0f + static_cast<float>(i % 25) * 22, 20.0f + static_cast<float>(i / 25) * 12,
                         "Нагрузка", "много", 1'700'000'000 + i));
            }
            QVERIFY(ot::Saver::save(*core, std::filesystem::path(path.toStdWString()),
                                    {.mode = ot::SaveMode::Full})
                        .ok());
        }
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        w.resize(1440, 900);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(path));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        CommentsPane* pane = showPane(w);
        QElapsedTimer load;
        load.start();
        QVERIFY(waitLoaded(pane, 1500));
        QCOMPARE(pane->shownCount(), 1500);
        QVERIFY2(load.elapsed() < 10000, qPrintable(QString::number(load.elapsed())));
        QVERIFY(pane->list()->uniformRowHeights());
        QElapsedTimer filter;
        filter.start();
        pane->setSearchText(QStringLiteral("нет такого"));
        QCOMPARE(pane->shownCount(), 0);
        pane->setSearchText(QString());
        QCOMPARE(pane->shownCount(), 1500);
        QVERIFY2(filter.elapsed() < 5000, qPrintable(QString::number(filter.elapsed())));
    }
};

OT_UI_TEST(CommentsPaneTest, "comments_pane");
#include "comments_pane_test.moc"
