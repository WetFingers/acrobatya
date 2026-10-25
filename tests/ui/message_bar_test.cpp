// ui.message_bar — стек полос сообщений над документом (план 2.4; эталон 6.1 «Документ», 6.6): полоса на вид
// — одна (повторный показ меняет текст), порядок — по важности независимо от порядка показа (защищённый
// просмотр, подпись недействительна, восстановлен, JavaScript, подпись с предупреждением, подписано, PDF/A,
// форма, скан, только просмотр, прочие), тоны видов (жёлтая, красная, синяя, фиолетовая), × и remove убирают,
// пустой стек скрыт; во вкладке документа — полоса «только просмотр» у не-PDF; сцена message-bar-stack.
#include <QDir>
#include <QPushButton>
#include <QtTest/QtTest>

#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/message_bar.h"
#include "ui/shots/scenes.h"
#include "ui/widgets/message_bar.h"
#include "ui/widgets/tool_button.h"

using ot::ui::MessageBar;
using ot::ui::MessageKind;
using ot::ui::MessageStack;
using ot::ui::Tone;

namespace {

QStringList order(const MessageStack& s) {
    QStringList out;
    for (MessageBar* b : s.bars()) {
        out << b->text();
    }
    return out;
}

}  // namespace

class MessageBarTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-message_bar"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
    }

    void priorityOrderAndTones() {
        MessageStack s;
        s.resize(800, 400);
        s.show();
        QVERIFY(!s.isVisible() || s.bars().isEmpty());
        s.add(Tone::Ok, QStringLiteral("custom"));
        s.showMessage(MessageKind::Form, QStringLiteral("form"));
        s.showMessage(MessageKind::LooksScanned, QStringLiteral("scan"));
        s.showMessage(MessageKind::PdfA, QStringLiteral("pdfa"));
        s.showMessage(MessageKind::SignatureInvalid, QStringLiteral("sig-bad"));
        s.showMessage(MessageKind::ProtectedView, QStringLiteral("protected"));
        s.showMessage(MessageKind::Repaired, QStringLiteral("repaired"));
        s.showMessage(MessageKind::JavaScriptOff, QStringLiteral("js"));
        s.showMessage(MessageKind::Xfa, QStringLiteral("xfa"));
        s.showMessage(MessageKind::SignatureValid, QStringLiteral("sig-ok"));
        s.showMessage(MessageKind::SignatureWarning, QStringLiteral("sig-warn"));
        s.showMessage(MessageKind::ViewOnly, QStringLiteral("view-only"));
        QCOMPARE(
            order(s),
            (QStringList{QStringLiteral("protected"), QStringLiteral("sig-bad"), QStringLiteral("repaired"),
                         QStringLiteral("js"), QStringLiteral("xfa"), QStringLiteral("sig-warn"),
                         QStringLiteral("sig-ok"), QStringLiteral("pdfa"), QStringLiteral("form"),
                         QStringLiteral("scan"), QStringLiteral("view-only"), QStringLiteral("custom")}));
        QCOMPARE(s.bar(MessageKind::ProtectedView)->tone(), Tone::Warn);
        QCOMPARE(s.bar(MessageKind::SignatureInvalid)->tone(), Tone::Danger);
        QCOMPARE(s.bar(MessageKind::SignatureValid)->tone(), Tone::Info);
        QCOMPARE(s.bar(MessageKind::PdfA)->tone(), Tone::Info);
        QCOMPARE(s.bar(MessageKind::Form)->tone(), Tone::Form);
        QCOMPARE(s.bar(MessageKind::LooksScanned)->tone(), Tone::Info);
        QCOMPARE(s.bar(MessageKind::JavaScriptOff)->tone(), Tone::Warn);
        QCOMPARE(s.bar(MessageKind::Xfa)->tone(), Tone::Warn);
        QCOMPARE(s.bar(MessageKind::Repaired)->tone(), Tone::Warn);
        QCOMPARE(s.bars().last()->tone(), Tone::Ok);
        QCOMPARE(s.bars().first()->property("tone").toString(), QStringLiteral("warn"));
    }

    void sameKindReplacesAndCloses() {
        MessageStack s;
        s.show();
        MessageBar* form = s.showMessage(MessageKind::Form, QStringLiteral("one"));
        form->addAction(QStringLiteral("Подсветить поля"), QStringLiteral("highlight"));
        MessageBar* again = s.showMessage(MessageKind::Form, QStringLiteral("two"));
        QCOMPARE(again, form);
        QCOMPARE(s.bars().size(), 1);
        QCOMPARE(form->text(), QStringLiteral("two"));
        QTRY_VERIFY(form->findChildren<QPushButton*>().isEmpty());  // прежние кнопки — убраны
        s.showMessage(MessageKind::PdfA, QStringLiteral("pdfa"));
        QSignalSpy changed(&s, &MessageStack::changed);
        s.bar(MessageKind::PdfA)->closeButton()->click();
        QCOMPARE(s.bars().size(), 1);
        QVERIFY(s.bar(MessageKind::PdfA) == nullptr);
        QVERIFY(changed.size() >= 1);
        s.remove(MessageKind::Form);
        QVERIFY(s.bars().isEmpty());
        QVERIFY(!s.isVisible());
        s.add(Tone::Info, QStringLiteral("a"));
        s.add(Tone::Info, QStringLiteral("b"));
        QCOMPARE(s.bars().size(), 2);  // свои — каждая отдельно
    }

    void documentViewUsesStack() {
        ot::ui::MainWindow w;
        w.resize(1440, 900);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        const QStringList images = QDir(QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/images"))
                                       .entryList({QStringLiteral("*.png")});
        QVERIFY(!images.isEmpty());
        QVERIFY(w.openFile(QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/images/") + images.front()));
        ot::ui::DocumentView* doc = w.currentDocument();
        MessageStack& s = doc->messageStack();
        QVERIFY(s.bar(MessageKind::ViewOnly) != nullptr);
        QCOMPARE(s.bar(MessageKind::ViewOnly)->objectName(), QStringLiteral("viewOnlyBar"));
        s.showMessage(MessageKind::ProtectedView, QStringLiteral("protected"));
        QCOMPARE(doc->messages().first()->text(), QStringLiteral("protected"));
        QCOMPARE(doc->messages().size(), 2);
        QVERIFY(s.isVisible());
    }

    void sceneShowsStack() {
        std::unique_ptr<QWidget> w(ot::ui::makeScene(QStringLiteral("message-bar-stack")));
        QVERIFY(w != nullptr);
        auto* window = static_cast<ot::ui::MainWindow*>(w.get());
        const QList<MessageBar*> bars = window->currentDocument()->messages();
        QCOMPARE(bars.size(), 4);
        QCOMPARE(MessageStack::kindOf(bars[0]), MessageKind::ProtectedView);
        QCOMPARE(MessageStack::kindOf(bars[3]), MessageKind::Form);
        window->setCloseConfirm(
            [](ot::ui::DocumentView&) { return ot::ui::MainWindow::CloseAnswer::Discard; });
    }
};

OT_UI_TEST(MessageBarTest, "message_bar");
#include "message_bar_test.moc"
