// ui.submit_dialog — отправка формы (план 6.1, п. 4; эталон 7.4, 7.5; forms-30, cloud-08) на
// tests/fixtures/forms/forms-all.pdf: окно подтверждения показывает адрес или получателя, формат и число полей;
// кнопка «Отправить: e-mail» — письмо с FDF-вложением (FakeMailer), «Отправить: https» — POST
// application/x-www-form-urlencoded (FakeHttpFetcher), ответ сервера с FDF — в форму; «Отмена» — ничего не уходит;
// в защищённом просмотре — отказ; сеть выключена — честное сообщение.
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "core/doc/document.h"
#include "core/forms/data_io.h"
#include "core/forms/fields.h"
#include "core/forms/submit.h"
#include "core/util/features.h"
#include "core/util/http_fetcher.h"
#include "services/app_info.h"
#include "services/forms_ops.h"
#include "services/http_fetcher.h"
#include "services/mail.h"
#include "support/ui_test.h"
#include "ui/dialogs/submit_dialog.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/tools/forms/form_fill_tool.h"
#include "ui/view/page_view.h"

using ot::ui::DocumentView;
using ot::ui::FormFillTool;
using ot::ui::MainWindow;
using ot::ui::SubmitDialog;
namespace forms = ot::forms;

namespace {

QString fixture() { return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/forms/forms-all.pdf"); }
QString dataDir() { return QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-submit-dialog"); }

DocumentView* openDoc(MainWindow& w) {
    DocumentView* doc = nullptr;
    [&] {
        w.resize(1440, 900);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(fixture()));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->forms().isLoaded());
        doc = w.currentDocument();
    }();
    return doc;
}

forms::SubmitPlan planFor(const char* button) {
    const auto doc = ot::Document::openOrThrow(std::filesystem::path(fixture().toStdWString()));
    return forms::planSubmit(*doc, forms::readField(*doc, button)->action, "forms-all.pdf");
}

void press(DocumentView& doc, const char* button) {
    const forms::Widget w = doc.forms().form().find(button)->widgets.front();
    doc.forms().activate(w.page, w.objnum);
}

}  // namespace

class SubmitDialogTest : public QObject {
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

    void dialogShowsTargetFormatAndFields() {
        OT_FEATURE("forms-30");
        const forms::SubmitPlan http = planFor("submit_http");
        SubmitDialog d(http);
        QVERIFY(d.targetLabel()->toolTip().contains(QStringLiteral("example.org/ottisk/submit")));
        QVERIFY2(d.detailsLabel()->text().contains(QStringLiteral("HTML")), qPrintable(d.detailsLabel()->text()));
        QCOMPARE(d.sendButton()->text(), QStringLiteral("&Отправить"));
        const forms::SubmitPlan mail = planFor("submit_mail");
        SubmitDialog m(mail);
        QCOMPARE(m.targetLabel()->text(), QStringLiteral("forms@example.org"));
        QVERIFY(m.detailsLabel()->text().contains(QStringLiteral("forms-all.fdf")));
        QCOMPARE(m.sendButton()->text(), QStringLiteral("&Создать письмо"));
    }

    void mailSubmitMakesLetterWithFdf() {
        OT_FEATURE("cloud-08");
        ot::FakeMailer mailer;
        ot::ScopedMailer scoped(mailer);
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w);
        QVERIFY(doc != nullptr);
        QString confirmedFor;
        doc->forms().setSubmitConfirm([&](const forms::SubmitPlan& p) {
            confirmedFor = QString::fromStdString(p.url);
            return true;
        });
        QSignalSpy done(&doc->forms(), &FormFillTool::submitted);
        press(*doc, "submit_mail");
        QTRY_COMPARE(done.count(), 1);
        QVERIFY(done.at(0).at(0).toBool());
        QVERIFY(confirmedFor.startsWith(QStringLiteral("mailto:forms@example.org")));
        checkMail(mailer);
    }

    void httpSubmitPostsAndImportsReply() {
        OT_FEATURE("forms-30");
        ot::net::FakeHttpFetcher fetcher;
        const std::string reply = forms::writeData(
            forms::FormData{{forms::FieldValue{"comment", {"Принято сервером"}, false}}, "", {}},
            forms::DataFormat::Fdf);
        fetcher.addBody("https://example.org/ottisk/submit", std::vector<std::uint8_t>(reply.begin(), reply.end()),
                        "application/vnd.fdf");
        ot::ScopedHttpFetcher scoped(fetcher);
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w);
        QVERIFY(doc != nullptr);
        doc->forms().setSubmitConfirm([](const forms::SubmitPlan&) { return true; });
        QSignalSpy done(&doc->forms(), &FormFillTool::submitted);
        press(*doc, "submit_http");
        QTRY_COMPARE(done.count(), 1);
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        QCOMPARE(fetcher.requestCount(), std::size_t{1});
        const ot::net::Request req = fetcher.requests().front();
        QCOMPARE(QString::fromStdString(req.method), QStringLiteral("POST"));
        QCOMPARE(QString::fromStdString(req.contentType), QStringLiteral("application/x-www-form-urlencoded"));
        const std::string body(req.body.begin(), req.body.end());
        QVERIFY2(body.find("name=Ivan%20Petrov") != std::string::npos, body.c_str());
        QTRY_COMPARE(QString::fromStdString(forms::read(doc->session()).result().find("comment")->value),
                     QStringLiteral("Принято сервером"));
    }

    void declinedSendsNothing() {
        ot::net::FakeHttpFetcher fetcher;
        ot::ScopedHttpFetcher scoped(fetcher);
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w);
        QVERIFY(doc != nullptr);
        bool asked = false;
        doc->forms().setSubmitConfirm([&asked](const forms::SubmitPlan&) {
            asked = true;
            return false;
        });
        QSignalSpy done(&doc->forms(), &FormFillTool::submitted);
        press(*doc, "submit_http");
        QTRY_VERIFY(asked);
        QTest::qWait(200);
        QCOMPARE(fetcher.requestCount(), std::size_t{0});
        QCOMPARE(done.count(), 0);
    }

    void protectedViewRefuses() {
        ot::net::FakeHttpFetcher fetcher;
        ot::ScopedHttpFetcher scoped(fetcher);
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w);
        QVERIFY(doc != nullptr);
        doc->setProtectedView(ot::protected_view::Origin::Internet);
        doc->forms().setSubmitConfirm([](const forms::SubmitPlan&) { return true; });
        QSignalSpy done(&doc->forms(), &FormFillTool::submitted);
        doc->forms().submit(doc->forms().form().find("submit_http")->action);
        QCOMPARE(done.count(), 1);
        QCOMPARE(done.at(0).at(0).toBool(), false);
        QCOMPARE(fetcher.requestCount(), std::size_t{0});
    }

    void networkOffIsReported() {
        ot::QtHttpFetcher off(ot::NetworkAccess::Off);
        ot::ScopedHttpFetcher scoped(off);
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w);
        QVERIFY(doc != nullptr);
        doc->forms().setSubmitConfirm([](const forms::SubmitPlan&) { return true; });
        QSignalSpy done(&doc->forms(), &FormFillTool::submitted);
        press(*doc, "submit_http");
        QTRY_COMPARE(done.count(), 1);
        QCOMPARE(done.at(0).at(0).toBool(), false);
        QVERIFY(done.at(0).at(1).toString().contains(QStringLiteral("Сеть выключена")));
    }

private:
    static void checkMail(const ot::FakeMailer& mailer) {
        QCOMPARE(mailer.messages().size(), std::size_t{1});
        const ot::MailMessage& m = mailer.messages().front();
        QCOMPARE(m.to, QStringList({QStringLiteral("forms@example.org")}));
        QCOMPARE(m.subject, QStringLiteral("Ottisk form"));
        QCOMPARE(m.attachments.size(), 1);
        QCOMPARE(m.attachments.front().name, QStringLiteral("forms-all.fdf"));
        QVERIFY(mailer.attachmentBytes().front().startsWith("%FDF-1.2"));
    }
};

OT_UI_TEST(SubmitDialogTest, "submit_dialog");

#include "submit_dialog_test.moc"
