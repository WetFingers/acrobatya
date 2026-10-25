// ui.replies_status — ответы и статусы (план 5.4, п. 3; эталон 3.4 «Попапы и ответы»; comment-20,
// comment-21): поле ответа переводит жирный, курсив, подчёркивание и цвет в /RC и обратно; ответ из панели —
// /Text с /IRT, автор Identity, /RC с font-weight:bold, шаг «Add reply»; «Изменить текст» — /Contents и /RC
// одним шагом; «Статус ▸ Принято» и «Отметить галочкой» — записи истории, видны в «Свойствах → История
// проверок»; «Отметить как непрочитанное» — локально и переживает новую службу; окно записки показывает
// ветку и отвечает по Enter; «Ответить» в меню «Выбора объектов» открывает поле ответа панели.
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTextEdit>
#include <QTreeWidget>
#include <QtTest/QtTest>

#include <string>
#include <vector>

#include "core/annot/annots.h"
#include "core/annot/comments.h"
#include "core/annot/defaults.h"
#include "core/util/features.h"
#include "services/annot_ops.h"
#include "services/app_info.h"
#include "services/comment_settings.h"
#include "services/comments_ops.h"
#include "services/document_session.h"
#include "services/read_state.h"
#include "services/settings.h"
#include "support/ui_test.h"
#include "ui/dialogs/annot_props_dialog.h"
#include "ui/panes/comments_pane.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/nav_pane.h"
#include "ui/tools/comment/popup_widget.h"
#include "ui/tools/comment/reply_editor.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/tool_button.h"

using ot::annot::AnnotProps;
using ot::annot::AnnotRef;
using ot::annot::AnnotType;
using ot::annot::ReviewState;
using ot::annot::RichSpan;
using ot::ui::CommentsPane;
using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::ReplyEditor;

namespace {

DocumentView* open(MainWindow& w) {
    DocumentView* doc = nullptr;
    [&] {
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        w.resize(1440, 900);
        w.show();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/demo/report.pdf")));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        doc = w.currentDocument();
        doc->view().setZoomPercent(100);
        doc->view().goToPage(0);
    }();
    return doc;
}

AnnotRef addNote(DocumentView& doc, const char* author, const char* text) {
    AnnotProps p = ot::annot::toolDefaults(AnnotType::Text);
    p.rect = {80, 120, 100, 140};
    p.author = author;
    p.contents = text;
    return ot::annot::create(doc.session(), 0, p).result();
}

CommentsPane* pane(MainWindow& w) {
    w.navPane().showPane(QStringLiteral("comments"));
    return qobject_cast<CommentsPane*>(w.navPane().page(QStringLiteral("comments")));
}

bool waitFor(CommentsPane* p, const std::function<bool()>& ok) {
    return QTest::qWaitFor([&] { return p->isLoaded() && ok(); }, 10000);
}

QAction* findAction(QMenu& menu, const QString& name) {
    for (QAction* a : menu.findChildren<QAction*>(name)) {
        return a;
    }
    return nullptr;
}

}  // namespace

class RepliesStatusTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-replies_status"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void editorRichTextRoundTrip() {
        OT_FEATURE("comment-20");
        ReplyEditor editor;
        editor.show();
        editor.editor()->insertPlainText(QStringLiteral("Обычный "));
        editor.boldButton()->click();
        editor.editor()->insertPlainText(QStringLiteral("жирный"));
        editor.boldButton()->click();
        editor.italicButton()->click();
        editor.underlineButton()->click();
        editor.editor()->insertPlainText(QStringLiteral(" курсив"));
        editor.italicButton()->click();
        editor.underlineButton()->click();
        editor.colorButton()->click();
        editor.editor()->insertPlainText(QStringLiteral(" цвет"));
        const std::vector<RichSpan> spans = editor.spans();
        QCOMPARE(static_cast<int>(spans.size()), 4);
        QCOMPARE(QString::fromStdString(spans[1].text), QStringLiteral("жирный"));
        QVERIFY(spans[1].bold && !spans[1].italic);
        QVERIFY(spans[2].italic && spans[2].underline);
        QVERIFY(spans[3].color != ot::annot::kNoRichColor);
        QCOMPARE(QString::fromStdString(editor.plainText()), QStringLiteral("Обычный жирный курсив цвет"));
        const std::string rc = editor.richContents();
        QVERIFY(rc.find("font-weight:bold") != std::string::npos);
        QVERIFY(rc.find("font-style:italic") != std::string::npos);
        QCOMPARE(ot::annot::parseRichContents(rc), spans);
        // Обратно: /RC → поле → те же отрезки; без оформления /RC не нужен.
        ReplyEditor back;
        back.setSpans(ot::annot::parseRichContents(rc));
        QCOMPARE(back.spans(), spans);
        back.setSpans({{.text = "просто\nтекст"}});
        QVERIFY(back.richContents().empty());
        QCOMPARE(QString::fromStdString(back.plainText()), QStringLiteral("просто\nтекст"));
        // Ctrl+Enter — отправить, Esc — отмена.
        QSignalSpy submitted(&back, &ReplyEditor::submitted);
        QSignalSpy cancelled(&back, &ReplyEditor::cancelled);
        QTest::keyClick(back.editor(), Qt::Key_Return, Qt::ControlModifier);
        QTest::keyClick(back.editor(), Qt::Key_Escape);
        QCOMPARE(submitted.count(), 1);
        QCOMPARE(cancelled.count(), 1);
    }

    void replyAndEditFromPane() {
        OT_FEATURE("comment-20");
        MainWindow w;
        DocumentView* doc = open(w);
        QVERIFY(doc != nullptr);
        CommentsPane* p = pane(w);
        const AnnotRef note = addNote(*doc, "Борис", "Нужна ссылка");
        QVERIFY(waitFor(p, [&] { return p->comment(note) != nullptr; }));
        QMenu menu;
        p->fillRecordMenu(menu, note);
        QAction* reply = findAction(menu, QStringLiteral("commentsReply"));
        QVERIFY(reply != nullptr && reply->isEnabled());
        reply->trigger();
        QVERIFY(p->isReplyOpen());
        QVERIFY(p->replyEditor()->titleLabel()->text().contains(QStringLiteral("Борис")));
        p->replyEditor()->setSpans({{.text = "@Борис "}, {.text = "готово", .bold = true}});
        p->replyEditor()->submitButton()->click();
        QVERIFY(!p->isReplyOpen());
        QVERIFY(
            waitFor(p, [&] { return p->comment(note) != nullptr && p->comment(note)->replyCount() == 1; }));
        const ot::annot::Comment& r = p->comment(note)->replies.front();
        QCOMPARE(QString::fromStdString(r.contents), QStringLiteral("@Борис готово"));
        QCOMPARE(r.author, ot::comments::author(ot::Settings()));
        QVERIFY(r.richContents.find("font-weight:bold") != std::string::npos);
        QVERIFY((r.flags & ot::annot::kAnnotHidden) != 0);
        QCOMPARE(doc->session().undoState().undoName, QString::fromLatin1(ot::comments::kReplyStep));

        // «Изменить текст» ответа: поле с прежним текстом, запись — /Contents и /RC.
        const AnnotRef replyRef = r.ref;
        p->startEdit(replyRef);
        QVERIFY(p->isReplyOpen());
        QCOMPARE(QString::fromStdString(p->replyEditor()->plainText()), QStringLiteral("@Борис готово"));
        p->replyEditor()->setSpans({{.text = "исправлено", .italic = true}});
        p->replyEditor()->submitButton()->click();
        QVERIFY(waitFor(p, [&] {
            const ot::annot::Comment* c = p->comment(replyRef);
            return c != nullptr && c->contents == "исправлено";
        }));
        QVERIFY(p->comment(replyRef)->richContents.find("font-style:italic") != std::string::npos);
        QCOMPARE(doc->session().undoState().undoName, QString::fromLatin1(ot::comments::kEditCommentStep));
    }

    void statusCheckAndHistory() {
        OT_FEATURE("comment-21");
        MainWindow w;
        DocumentView* doc = open(w);
        QVERIFY(doc != nullptr);
        CommentsPane* p = pane(w);
        const AnnotRef note = addNote(*doc, "Борис", "Статус");
        QVERIFY(waitFor(p, [&] { return p->comment(note) != nullptr; }));
        {
            QMenu menu;
            p->fillRecordMenu(menu, note);
            QMenu* status = menu.findChild<QMenu*>(QStringLiteral("commentStatusMenu"));
            QVERIFY(status != nullptr && status->isEnabled());
            QCOMPARE(status->actions().size(), ot::annot::kReviewStateCount);
            QAction* accepted = status->findChild<QAction*>(QStringLiteral("commentStatusAccepted"));
            QVERIFY(accepted != nullptr);
            accepted->trigger();
        }
        QVERIFY(waitFor(p, [&] {
            return p->comment(note) != nullptr && p->comment(note)->review() == ReviewState::Accepted;
        }));
        QCOMPARE(doc->session().undoState().undoName, QString::fromLatin1(ot::comments::kStatusStep));
        {
            QMenu menu;
            p->fillRecordMenu(menu, note);
            QAction* check = findAction(menu, QStringLiteral("commentCheckMark"));
            QVERIFY(check != nullptr && !check->isChecked());
            check->trigger();
        }
        QVERIFY(waitFor(p, [&] { return p->comment(note) != nullptr && p->comment(note)->marked(); }));
        QCOMPARE(doc->session().undoState().undoName, QString::fromLatin1(ot::comments::kCheckStep));
        // Статус-записки на странице не видны.
        for (const ot::annot::AnnotInfo& info : ot::annot::list(doc->session(), 0).result()) {
            if (info.inReplyTo == note.objnum) {
                QVERIFY(info.hiddenOnScreen());
            }
        }
        // «Свойства → История проверок»: две записи.
        auto* dialog = ot::ui::AnnotPropsDialog::openFor(doc->view(), note);
        QTRY_VERIFY(dialog->historyList()->topLevelItemCount() == 2);
        QVERIFY(!dialog->historyList()->isHidden());
        QCOMPARE(dialog->historyList()->topLevelItem(0)->text(2), QStringLiteral("Принято"));
        QCOMPARE(dialog->historyList()->topLevelItem(0)->text(1),
                 QString::fromStdString(ot::comments::author(ot::Settings())));
        dialog->reject();
    }

    void unreadToggleIsLocal() {
        OT_FEATURE("comment-21");
        MainWindow w;
        DocumentView* doc = open(w);
        QVERIFY(doc != nullptr);
        CommentsPane* p = pane(w);
        const AnnotRef note = addNote(*doc, "Чужой автор", "непрочитано");
        QVERIFY(waitFor(p, [&] { return p->comment(note) != nullptr; }));
        QVERIFY(p->isUnread(*p->comment(note)));
        {
            QMenu menu;
            p->fillRecordMenu(menu, note);
            QAction* toggle = findAction(menu, QStringLiteral("commentsToggleRead"));
            QVERIFY(toggle != nullptr);
            toggle->trigger();
        }
        QVERIFY(!p->isUnread(*p->comment(note)));
        {
            QMenu menu;
            p->fillRecordMenu(menu, note);
            findAction(menu, QStringLiteral("commentsToggleRead"))->trigger();
        }
        QVERIFY(p->isUnread(*p->comment(note)));
        // Отметка — в state.sqlite, файл не правится.
        QVERIFY(!doc->session().undoState().undoName.contains(QStringLiteral("read")));
        ot::comments::ReadState again;
        again.setDocument(p->readState().document());
        QCOMPARE(again.stored(note), std::optional<bool>(false));
    }

    void popupThreadAndReply() {
        OT_FEATURE("comment-20");
        MainWindow w;
        DocumentView* doc = open(w);
        QVERIFY(doc != nullptr);
        CommentsPane* p = pane(w);
        const AnnotRef note = addNote(*doc, "Борис", "Записка с веткой");
        ot::comments::reply(doc->session(), note, {.author = "Анна", .contents = "первый ответ"})
            .waitForFinished();
        ot::ui::PopupLayer& popups = ot::ui::PopupLayer::of(doc->view());
        popups.open(note, false);
        QTRY_VERIFY(popups.popup(note) != nullptr &&
                    popups.popup(note)->repliesLabel()->isVisibleTo(popups.popup(note)));
        ot::ui::PopupWidget* popup = popups.popup(note);
        QVERIFY(popup->repliesLabel()->text().contains(QStringLiteral("первый ответ")));
        QVERIFY(popup->repliesLabel()->text().contains(QStringLiteral("Анна")));
        popup->replyField()->setText(QStringLiteral("второй ответ"));
        QTest::keyClick(popup->replyField(), Qt::Key_Return);
        QTRY_VERIFY(popup->repliesLabel()->text().contains(QStringLiteral("второй ответ")));
        QVERIFY(
            waitFor(p, [&] { return p->comment(note) != nullptr && p->comment(note)->replyCount() == 2; }));
        QVERIFY(popup->replyField()->text().isEmpty());
    }

    void pageMenuOpensReply() {
        OT_FEATURE("comment-20");
        MainWindow w;
        DocumentView* doc = open(w);
        QVERIFY(doc != nullptr);
        CommentsPane* p = pane(w);
        const AnnotRef note = addNote(*doc, "Борис", "Через меню страницы");
        QVERIFY(waitFor(p, [&] { return p->comment(note) != nullptr; }));
        w.navPane().closePanel();
        auto& select = doc->view().tools().annotSelect();
        for (const ot::annot::AnnotInfo& info : ot::annot::list(doc->session(), 0).result()) {
            if (info.ref == note) {
                select.setSelection({info});
            }
        }
        QMenu menu;
        select.fillContextMenu(menu);
        QAction* reply = findAction(menu, QStringLiteral("commentReply"));
        QVERIFY(reply != nullptr && reply->isEnabled());
        QVERIFY(menu.findChild<QMenu*>(QStringLiteral("commentStatusMenu")) != nullptr);
        reply->trigger();
        QVERIFY(w.navPane().isPanelOpen());
        QCOMPARE(w.navPane().currentPane(), QStringLiteral("comments"));
        QTRY_VERIFY(p->isReplyOpen());
        QCOMPARE(p->replyTarget(), std::optional<AnnotRef>(note));
    }
};

OT_UI_TEST(RepliesStatusTest, "replies_status");
#include "replies_status_test.moc"
