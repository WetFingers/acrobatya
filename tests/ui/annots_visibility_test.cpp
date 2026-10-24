// ui.annots_visibility — «Показать/скрыть все комментарии» (план 5.1, п. 5; эталон 3.4 «Панель
// "Комментарии"», 6.8 Ctrl+Shift+8; comment-19): команда comment.toggleAll прячет комментарии вкладки —
// снимок страницы annots-all.pdf меняется, а файл нет (истории правок нет, /F аннотаций прежние); ключ тайлов
// несёт маску скрытых подтипов; скрытый комментарий не выбирается «Выбором объектов», окна записок
// закрываются; фильтр по подтипам (основа 5.4) прячет только выбранные; повторная команда возвращает снимок
// как был.
#include <QDir>
#include <QSettings>
#include <QtTest/QtTest>

#include "core/annot/annots.h"
#include "core/util/features.h"
#include "services/annot_ops.h"
#include "services/app_info.h"
#include "services/commands.h"
#include "services/document_session.h"
#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/tools/comment/popup_widget.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"
#include "ui/view/tool_controller.h"

using ot::annot::AnnotInfo;
using ot::annot::AnnotType;
using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::PageView;
using ot::ui::ViewToolId;

namespace {

DocumentView* openMatrix(MainWindow& w) {
    DocumentView* doc = nullptr;
    [&] {
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        w.resize(1440, 900);
        w.show();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/annots/annots-all.pdf")));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        doc = w.currentDocument();
        doc->view().setZoomPercent(100);
        doc->view().goToPage(0);
    }();
    return doc;
}

// Кадр вьюпорта, когда видимые тайлы готовы.
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

AnnotInfo bySubtype(DocumentView& doc, AnnotType type) {
    for (const AnnotInfo& a : ot::annot::list(doc.session(), 0).result()) {
        if (a.type == type) {
            return a;
        }
    }
    return {};
}

QPoint center(const PageView& v, const ot::RectF& r) {
    return v.pageTransform(0).map(QPointF((r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2)).toPoint();
}

}  // namespace

class AnnotsVisibilityTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-annots_visibility"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void toggleAllChangesFrameNotFile() {
        OT_FEATURE("comment-19");
        MainWindow w;
        DocumentView* doc = openMatrix(w);
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        const QImage shown = frame(v);
        const auto flagsBefore = ot::annot::list(doc->session(), 0).result();
        const quint64 revision = doc->session().revision();

        QVERIFY(!doc->commentsHidden());
        QVERIFY(w.commands().trigger(QStringLiteral("comment.toggleAll")));
        QVERIFY(doc->commentsHidden());
        QCOMPARE(v.tilePainter().annotsHidden(), DocumentView::commentTypesMask());
        const QImage hidden = frame(v);
        QVERIFY(hidden != shown);
        // Файл не тронут: ни правки, ни новых флагов.
        QCOMPARE(doc->session().revision(), revision);
        QVERIFY(!doc->session().undoState().canUndo);
        const auto flagsAfter = ot::annot::list(doc->session(), 0).result();
        QCOMPARE(flagsAfter.size(), flagsBefore.size());
        for (std::size_t i = 0; i < flagsAfter.size(); ++i) {
            QCOMPARE(flagsAfter[i].flags, flagsBefore[i].flags);
        }
        // Скрытый не выбирается.
        v.tools().setTool(ViewToolId::AnnotSelect);
        const AnnotInfo square = bySubtype(*doc, AnnotType::Square);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, center(v, square.rect));
        QTest::qWait(300);
        QVERIFY(v.tools().annotSelect().selection().empty());

        QVERIFY(w.commands().trigger(QStringLiteral("comment.toggleAll")));
        QVERIFY(!doc->commentsHidden());
        QCOMPARE(frame(v), shown);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, center(v, square.rect));
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(v.tools().annotSelect().selection().size()), 1, 10000);
    }

    void filterByTypeAndPopups() {
        MainWindow w;
        DocumentView* doc = openMatrix(w);
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        const QImage shown = frame(v);
        const AnnotInfo note = bySubtype(*doc, AnnotType::Text);
        ot::ui::PopupLayer::of(v).open(note.ref, false);
        QTRY_COMPARE_WITH_TIMEOUT(ot::ui::PopupLayer::of(v).count(), 1, 10000);
        // Только квадраты.
        doc->setAnnotsHidden(1u << static_cast<int>(AnnotType::Square));
        QVERIFY(!doc->commentsHidden());
        QCOMPARE(ot::ui::PopupLayer::of(v).count(), 0);  // фильтр закрывает окна записок
        const QImage noSquare = frame(v);
        QVERIFY(noSquare != shown);
        doc->setCommentsHidden(true);
        const QImage none = frame(v);
        QVERIFY(none != noSquare);
        doc->setAnnotsHidden(0);
        QCOMPARE(frame(v), shown);
    }
};

OT_UI_TEST(AnnotsVisibilityTest, "annots_visibility");
#include "annots_visibility_test.moc"
