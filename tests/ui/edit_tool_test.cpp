// ui.edit_tool — режим «Редактировать» инструмента «Редактировать PDF» (план 3.4, п. 1; эталон 3.9
// «Инструмент»; edit-01 — UI): вход в режим рисует рамки всех блоков фикстуры (latin.pdf — 3 абзаца,
// rotated.pdf — 3, из них 2 «только чтение» с замком и подсказкой); щелчок ставит каретку, набор «Test» через
// QTest::keyClicks меняет текст блока, после Esc правка записана шагом «Edit text» и видна в stext страницы,
// Ctrl+Z возвращает; Ctrl+Z до записи бросает незаписанную правку; IME (QInputMethodEvent) набирает
// кириллицу в латинский абзац (новый CID-шрифт) — stext; подписанный документ — «Текст» серый с подсказкой,
// каретка не ставится.
#include <QDir>
#include <QInputMethodEvent>
#include <QLabel>
#include <QSettings>
#include <QtTest/QtTest>

#include "core/text/stext.h"
#include "services/app_info.h"
#include "services/document_session.h"
#include "support/ui_test.h"
#include "ui/panes/find_bar.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/tools/edit/edit_mode.h"
#include "ui/tools/edit/edit_tool.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/tool_button.h"

using ot::ui::DocumentView;
using ot::ui::EditMode;
using ot::ui::EditTool;
using ot::ui::MainWindow;
using ot::ui::PageView;
using ot::ui::ToolRegistry;
using ot::ui::ViewToolId;

namespace {

QString fixture(const char* rel) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/") + QLatin1String(rel);
}

EditTool& editTool() {
    return *static_cast<EditTool*>(ToolRegistry::instance().find(QStringLiteral("edit")));
}

// Окно с документом и включённым «Редактировать PDF»; блоки первой страницы загружены.
DocumentView* openEdit(MainWindow& w, const char* rel) {
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
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("edit"), doc));
        QCOMPARE(doc->view().tools().tool(), ViewToolId::Edit);
        EditMode& mode = EditTool::modeOf(doc->view());
        QTRY_VERIFY_WITH_TIMEOUT(mode.hasContent(0), 10000);
        doc->view().setFocus();
    }();
    return doc;
}

std::string pageText(DocumentView& doc, int page = 0) {
    const std::shared_ptr<const ot::TextPage> text = doc.session().textPage(page).result();
    return text ? text->text() : std::string();
}

// Точка внутри первой строки блока (у левого края).
QPoint blockStart(const EditMode::Frame& f) { return (f.rect.topLeft() + QPointF(1.5, 3)).toPoint(); }

}  // namespace

class EditToolTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-edit_tool"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void cleanup() { ToolRegistry::instance().deactivate(); }

    void enterDrawsFramesForAllBlocks() {
        std::printf("OT_FEATURE:edit-01\n");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openEdit(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        EditMode& mode = EditTool::modeOf(doc->view());
        const std::vector<EditMode::Frame> frames = mode.frames();
        QCOMPARE(static_cast<int>(frames.size()), 3);
        for (const EditMode::Frame& f : frames) {
            QVERIFY(f.editable);
            QVERIFY(!f.rect.isEmpty());
        }
        QVERIFY(editTool().textButton()->isChecked());
        QCOMPARE(doc->view().viewport()->cursor().shape(), Qt::ArrowCursor);
        QTest::mouseMove(doc->view().viewport(), frames[0].rect.center().toPoint());
        QCOMPARE(doc->view().viewport()->cursor().shape(), Qt::IBeamCursor);
    }

    // Рубеж Р2: в панели «Редактировать PDF» есть «Найти и заменить» (эталон 6.2) — поле «Заменить на» панели
    // «Найти», как Ctrl+Shift+H.
    void panelFindReplaceOpensReplaceField() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openEdit(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        auto* button = editTool().panel()->findChild<ot::ui::ToolButton*>(QStringLiteral("editFindReplace"));
        QVERIFY(button != nullptr);
        QVERIFY(button->isEnabled());
        QVERIFY(!doc->findBar().replaceMode());
        button->click();
        QTRY_VERIFY(doc->findBar().replaceMode());
    }

    void readOnlyBlocksHaveLockAndTip() {
        MainWindow w;
        DocumentView* doc = openEdit(w, "edit/rotated.pdf");
        QVERIFY(doc != nullptr);
        EditMode& mode = EditTool::modeOf(doc->view());
        const std::vector<EditMode::Frame> frames = mode.frames();
        QCOMPARE(static_cast<int>(frames.size()), 3);
        int readOnly = 0;
        for (const EditMode::Frame& f : frames) {
            if (!f.editable) {
                ++readOnly;
                QCOMPARE(f.reason, QStringLiteral("rotated"));
                const QString tip = mode.readOnlyTip(f.rect.center());
                QVERIFY2(tip.startsWith(QStringLiteral("Только чтение")), qPrintable(tip));
                QVERIFY(tip.contains(QStringLiteral("Повёрнутый")));
            }
        }
        QCOMPARE(readOnly, 2);
        // Щелчок по блоку «только чтение» каретку не ставит.
        for (const EditMode::Frame& f : frames) {
            if (!f.editable) {
                QTest::mouseClick(doc->view().viewport(), Qt::LeftButton, {}, f.rect.center().toPoint());
                QVERIFY(!mode.editing());
            }
        }
    }

    void typingChangesStextAndUndo() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openEdit(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        EditMode& mode = EditTool::modeOf(v);
        const std::string before = pageText(*doc);
        QVERIFY(before.find("The editor builds") != std::string::npos);

        const EditMode::Frame first = mode.frames().front();
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, blockStart(first));
        QVERIFY(mode.editing());
        QCOMPARE(mode.caretPosition(), 0);
        QVERIFY(mode.capturesKeyboard());

        QTest::keyClicks(&v, QStringLiteral("Test "));
        QVERIFY(
            QString::fromStdU32String(mode.editor()->text()).startsWith(QStringLiteral("Test The editor")));
        QCOMPARE(mode.caretPosition(), 5);
        QVERIFY(mode.hasPendingEdit());
        QVERIFY(mode.showsLocalCopy());
        QVERIFY(!doc->session().undoState().canUndo);  // ещё не записано

        // Esc — выход из блока и запись; режим остаётся.
        QTest::keyClick(&v, Qt::Key_Escape);
        QVERIFY(!mode.editing());
        QCOMPARE(v.tools().tool(), ViewToolId::Edit);
        QTRY_VERIFY_WITH_TIMEOUT(!mode.hasPendingEdit(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(doc->session().undoState().canUndo, 10000);
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Edit text"));
        const std::string after = pageText(*doc);
        QVERIFY2(after.find("Test The editor") != std::string::npos, after.c_str());
        // Блоки страницы — заново (абзац вырос вниз и может слиться со следующим в один блок).
        QTRY_VERIFY_WITH_TIMEOUT(mode.hasContent(0), 10000);
        QVERIFY(mode.frames().size() >= 2);

        // Ctrl+Z (команда окна) возвращает текст.
        QTest::keyClick(&v, Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!doc->session().undoState().canUndo, 10000);
        const std::string undone = pageText(*doc);
        QVERIFY(undone.find("Test The editor") == std::string::npos);
        QVERIFY(undone.find("The editor builds") != std::string::npos);
    }

    void ctrlZBeforeCommitDropsLocalEdit() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openEdit(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        EditMode& mode = EditTool::modeOf(v);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, blockStart(mode.frames().front()));
        QVERIFY(mode.editing());
        const std::u32string original = mode.editor()->text();
        QTest::keyClicks(&v, QStringLiteral("abc"));
        QVERIFY(mode.editor()->text() != original);
        QTest::keyClick(&v, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(mode.editor()->text() == original);
        QVERIFY(!mode.hasPendingEdit());
        QTest::keyClick(&v, Qt::Key_Escape);
        QTest::qWait(50);
        QVERIFY(!doc->session().undoState().canUndo);
    }

    void selectionKeysAndBackspace() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openEdit(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        EditMode& mode = EditTool::modeOf(v);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, blockStart(mode.frames().front()));
        QVERIFY(mode.editing());
        // Shift+→ три раза — «The», Backspace стирает.
        for (int i = 0; i < 3; ++i) {
            QTest::keyClick(&v, Qt::Key_Right, Qt::ShiftModifier);
        }
        QCOMPARE(mode.selection(), (ot::edit::TextRange{0, 3}));
        QTest::keyClick(&v, Qt::Key_Backspace);
        QVERIFY(QString::fromStdU32String(mode.editor()->text()).startsWith(QStringLiteral(" editor")));
        // Ctrl+A — весь блок.
        QTest::keyClick(&v, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(mode.selection().begin, 0);
        QCOMPARE(mode.selection().end, mode.editor()->size());
        QVERIFY(mode.revertPending());
    }

    void imeTypesCyrillicIntoLatinParagraph() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openEdit(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        EditMode& mode = EditTool::modeOf(v);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, blockStart(mode.frames().front()));
        QVERIFY(mode.editing());
        QVERIFY(v.testAttribute(Qt::WA_InputMethodEnabled));
        QVERIFY(v.inputMethodQuery(Qt::ImEnabled).toBool());
        QVERIFY(v.inputMethodQuery(Qt::ImCursorRectangle).toRectF().isValid());
        // Составная строка, затем набранная.
        QInputMethodEvent preedit(QStringLiteral("При"), {});
        QApplication::sendEvent(&v, &preedit);
        QVERIFY(QString::fromStdU32String(mode.editor()->text()).startsWith(QStringLiteral("The")));
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("Привет, "));
        QApplication::sendEvent(&v, &commit);
        QVERIFY(QString::fromStdU32String(mode.editor()->text()).startsWith(QStringLiteral("Привет, The")));
        mode.leaveBlock();
        QTRY_VERIFY_WITH_TIMEOUT(!mode.hasPendingEdit(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(doc->session().undoState().canUndo, 10000);
        const QString text = QString::fromStdString(pageText(*doc));
        QVERIFY2(text.contains(QStringLiteral("Привет, The editor")), qPrintable(text.left(200)));
    }

    void signedDocumentIsDisabledWithTip() {
        MainWindow w;
        DocumentView* doc = openEdit(w, "sig/signed-bb.pdf");
        QVERIFY(doc != nullptr);
        EditTool& tool = editTool();
        QTRY_VERIFY_WITH_TIMEOUT(!tool.lockReason().isEmpty(), 10000);
        QVERIFY2(tool.lockReason().contains(QStringLiteral("подписан")), qPrintable(tool.lockReason()));
        QVERIFY(!tool.textButton()->isEnabled());
        QCOMPARE(tool.textButton()->toolTip(), tool.lockReason());
        QVERIFY(!tool.lockLabel()->isHidden());
        EditMode& mode = EditTool::modeOf(doc->view());
        QVERIFY(mode.locked());
        const std::vector<EditMode::Frame> frames = mode.frames();
        QVERIFY(!frames.empty());
        QTest::mouseClick(doc->view().viewport(), Qt::LeftButton, {}, blockStart(frames.front()));
        QVERIFY(!mode.editing());
        QVERIFY(!mode.enterBlock(frames.front().page, frames.front().block));
    }
};

OT_UI_TEST(EditToolTest, "edit_tool");
#include "edit_tool_test.moc"
