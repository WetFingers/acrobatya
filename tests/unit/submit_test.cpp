// unit.submit — отправка формы (план 6.1, п. 4; эталон 3.6, 7.4; forms-30, cloud-08): кнопки
// tests/fixtures/forms/forms-all.pdf — «submit_mail» (mailto:, флаги 0 → FDF вложением; получатель и тема из
// адреса) и «submit_http» (https, ExportFormat → HTML urlencoded POST); флаги XFDF и SubmitPDF дают свои типы;
// /Fields с «исключить» и без пустых значений; mailto с %-кодированием; FakeMailer получил письмо с FDF-вложением
// (services/mail.h), FakeHttpFetcher — POST нужного типа с телом; ответ сервера с FDF импортируется в форму.
#include <gtest/gtest.h>

#include <QFile>
#include <QFileInfo>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/forms/data_io.h"
#include "core/forms/fields.h"
#include "core/forms/submit.h"
#include "core/util/features.h"
#include "core/util/http_fetcher.h"
#include "services/document_session.h"
#include "services/forms_ops.h"
#include "services/mail.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
namespace forms = ot::forms;
using forms::SubmitFormat;
using ot::Document;

namespace {

fs::path fixture() { return ot::test::sourceDir() / "tests" / "fixtures" / "forms" / "forms-all.pdf"; }

forms::FieldAction actionOf(const Document& doc, const char* button) {
    return forms::readField(doc, button)->action;
}

bool hasField(const forms::SubmitPlan& p, const std::string& name) {
    return std::any_of(p.fields.begin(), p.fields.end(), [&name](const forms::FieldValue& v) { return v.name == name; });
}

}  // namespace

TEST(submit, mailto_fdf_plan) {
    OT_FEATURE("forms-30");
    const auto doc = Document::openOrThrow(fixture());
    const forms::FieldAction a = actionOf(*doc, "submit_mail");
    ASSERT_EQ(a.kind, forms::ActionKind::SubmitForm);
    const forms::SubmitPlan p = forms::planSubmit(*doc, a, "forms-all.pdf");
    EXPECT_EQ(p.target, forms::SubmitTarget::Mail);
    EXPECT_EQ(p.format, SubmitFormat::Fdf);
    EXPECT_EQ(p.contentType, "application/vnd.fdf");
    ASSERT_TRUE(p.mail.has_value());
    EXPECT_EQ(p.mail->to, std::vector<std::string>({"forms@example.org"}));
    EXPECT_EQ(p.mail->subject, "Ottisk form");
    EXPECT_EQ(p.attachmentName, "forms-all.fdf");
    EXPECT_EQ(p.body.substr(0, 8), "%FDF-1.2");
    const forms::FormData back = forms::readData(p.body, forms::DataFormat::Fdf);
    EXPECT_EQ(back.file, "forms-all.pdf");
    EXPECT_FALSE(back.fields.empty());
}

TEST(submit, https_html_post_plan) {
    OT_FEATURE("forms-30");
    const auto doc = Document::openOrThrow(fixture());
    const forms::SubmitPlan p = forms::planSubmit(*doc, actionOf(*doc, "submit_http"), "forms-all.pdf");
    EXPECT_EQ(p.target, forms::SubmitTarget::Https);
    EXPECT_EQ(p.format, SubmitFormat::Html);
    EXPECT_EQ(p.contentType, "application/x-www-form-urlencoded");
    EXPECT_FALSE(p.get);
    EXPECT_NE(p.body.find("name=Ivan%20Petrov"), std::string::npos) << p.body;
    EXPECT_NE(p.body.find("comment_ru=%D0%9F%D1%80%D0%B8%D0%B2%D0%B5%D1%82"), std::string::npos) << p.body;
    // Пустое поле пароля — без IncludeNoValueFields не отправляется.
    EXPECT_EQ(p.body.find("secret="), std::string::npos);
    EXPECT_FALSE(hasField(p, "secret"));
}

TEST(submit, flags_formats_and_field_selection) {
    const auto doc = Document::openOrThrow(fixture());
    forms::FieldAction a;
    a.kind = forms::ActionKind::SubmitForm;
    a.target = "https://example.org/x";
    a.flags = forms::submit_flags::kXfdf;
    forms::SubmitPlan p = forms::planSubmit(*doc, a, "forms-all.pdf");
    EXPECT_EQ(p.format, SubmitFormat::Xfdf);
    EXPECT_EQ(p.contentType, "application/xml");
    EXPECT_NE(p.body.find("<xfdf"), std::string::npos);
    a.flags = forms::submit_flags::kSubmitPdf;
    p = forms::planSubmit(*doc, a, "forms-all.pdf");
    EXPECT_EQ(p.format, SubmitFormat::Pdf);
    EXPECT_EQ(p.contentType, "application/pdf");
    EXPECT_TRUE(p.body.empty());
    // Только перечисленные; «исключить» — все, кроме них; пустые — с IncludeNoValueFields.
    a.flags = forms::submit_flags::kExportFormat | forms::submit_flags::kGetMethod;
    a.fields = {"name", "city"};
    p = forms::planSubmit(*doc, a, "forms-all.pdf");
    EXPECT_TRUE(p.get);
    EXPECT_EQ(p.fields.size(), 2u);
    EXPECT_EQ(p.url, "https://example.org/x?name=Ivan%20Petrov&city=Kazan");
    a.flags = forms::submit_flags::kExclude | forms::submit_flags::kIncludeNoValueFields;
    p = forms::planSubmit(*doc, a, "forms-all.pdf");
    EXPECT_FALSE(hasField(p, "name"));
    EXPECT_FALSE(hasField(p, "city"));
    EXPECT_TRUE(hasField(p, "secret"));
    EXPECT_TRUE(hasField(p, "comment"));
    a.target = "ftp://example.org/";
    EXPECT_EQ(forms::planSubmit(*doc, a, "x.pdf").target, forms::SubmitTarget::Unsupported);
}

TEST(submit, mailto_parsing) {
    const std::optional<forms::MailTo> m =
        forms::parseMailto("mailto:a@example.org,b@example.org?subject=%D0%90%D0%BD%D0%BA%D0%B5%D1%82%D0%B0&cc=c@"
                           "example.org&body=line%201%0Aline%202");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->to, std::vector<std::string>({"a@example.org", "b@example.org"}));
    EXPECT_EQ(m->cc, std::vector<std::string>({"c@example.org"}));
    EXPECT_EQ(m->subject, "Анкета");
    EXPECT_EQ(m->body, "line 1\nline 2");
    EXPECT_FALSE(forms::parseMailto("https://example.org").has_value());
    EXPECT_EQ(forms::urlEncodeForm({{"a b", {"1&2"}, false}, {"c", {"x", "y"}, false}}), "a%20b=1%262&c=x&c=y");
    EXPECT_TRUE(forms::isFdfResponse("application/vnd.fdf", ""));
    EXPECT_TRUE(forms::isFdfResponse("application/octet-stream", "%FDF-1.2\n"));
    EXPECT_FALSE(forms::isFdfResponse("text/html", "<html/>"));
}

TEST(submit, fake_mailer_gets_fdf_attachment) {
    OT_FEATURE("cloud-08");
    ot::test::qtApp();
    const auto doc = Document::openOrThrow(fixture());
    const forms::SubmitPlan p = forms::planSubmit(*doc, actionOf(*doc, "submit_mail"), "forms-all.pdf");
    ot::FakeMailer mailer;
    const ot::MailResult r = forms::sendMail(p, QByteArray(), mailer);
    EXPECT_EQ(r.status, ot::MailResult::Status::Sent);
    ASSERT_EQ(mailer.messages().size(), 1u);
    const ot::MailMessage& m = mailer.messages().front();
    EXPECT_EQ(m.to, QStringList({QStringLiteral("forms@example.org")}));
    EXPECT_EQ(m.subject, QStringLiteral("Ottisk form"));
    ASSERT_EQ(m.attachments.size(), 1);
    EXPECT_EQ(m.attachments.front().name, QStringLiteral("forms-all.fdf"));
    // Вложение — файл во временной папке: FDF документа (FakeMailer читает его при отправке).
    EXPECT_TRUE(mailer.attachmentBytes().front().startsWith("%FDF-1.2"));
    // Нет почтовой программы — честный отказ.
    mailer.setResult(ot::MailResult::Status::NoClient);
    EXPECT_EQ(forms::sendMail(p, QByteArray(), mailer).status, ot::MailResult::Status::NoClient);
}

TEST(submit, fake_http_gets_post_and_fdf_reply_is_imported) {
    OT_FEATURE("forms-30");
    ot::test::qtApp();
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(fixture().wstring())));
    const forms::FieldAction a = forms::read(session).result().find("submit_http")->action;
    const forms::SubmitPlan p = forms::planSubmit(session, a).result();
    ot::net::FakeHttpFetcher fetcher;
    const std::string reply = forms::writeData(forms::FormData{{forms::FieldValue{"name", {"Ответ сервера"}, false}}, "", {}},
                                               forms::DataFormat::Fdf);
    fetcher.addBody("https://example.org/ottisk/submit", std::vector<std::uint8_t>(reply.begin(), reply.end()),
                    "application/vnd.fdf");
    const ot::net::Response response = forms::sendHttp(p, QByteArray(), fetcher);
    ASSERT_TRUE(response.ok()) << response.error;
    ASSERT_EQ(fetcher.requestCount(), 1u);
    const ot::net::Request req = fetcher.requests().front();
    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.contentType, "application/x-www-form-urlencoded");
    EXPECT_EQ(req.purpose, ot::net::Purpose::General);
    const std::string body(req.body.begin(), req.body.end());
    EXPECT_NE(body.find("name=Ivan%20Petrov"), std::string::npos);
    ASSERT_TRUE(forms::isFdfResponse(response.contentType,
                                     std::string_view(reinterpret_cast<const char*>(response.body.data()),
                                                      response.body.size())));
    const forms::DataImportReport r =
        forms::importData(session, forms::readData(reply, forms::DataFormat::Fdf), QString()).result();
    EXPECT_EQ(r.updated, std::vector<std::string>({"name"}));
    EXPECT_EQ(forms::read(session).result().find("name")->value, "Ответ сервера");
}

TEST(submit, pdf_body_is_a_copy_of_the_document) {
    ot::test::qtApp();
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(fixture().wstring())));
    ASSERT_TRUE(forms::setValue(session, QStringLiteral("name"), QStringLiteral("В PDF")).result().accepted);
    forms::FieldAction a;
    a.kind = forms::ActionKind::SubmitForm;
    a.target = "https://example.org/pdf";
    a.flags = forms::submit_flags::kSubmitPdf;
    const forms::SubmitPlan p = forms::planSubmit(session, a).result();
    const QByteArray pdf = forms::submitBody(session, p).result();
    ASSERT_TRUE(pdf.startsWith("%PDF-"));
    // Правки документа остаются отменяемыми (копия, не запись).
    EXPECT_TRUE(session.undoState().canUndo);
    const auto copy = Document::openOrThrowMemory(pdf.toStdString());
    EXPECT_EQ(forms::readField(*copy, "name")->value, "В PDF");
    ot::net::FakeHttpFetcher fetcher;
    fetcher.addBody("https://example.org/pdf", {}, "text/plain");
    ASSERT_TRUE(forms::sendHttp(p, pdf, fetcher).ok());
    EXPECT_EQ(fetcher.requests().front().contentType, "application/pdf");
    EXPECT_EQ(fetcher.requests().front().body.size(), static_cast<std::size_t>(pdf.size()));
}
