// unit.js_bridge — JavaScript формы (план 6.1, п. 1; эталон 3.6, 7.2; forms-31, security-36, security-37,
// security-40): политика включения (по умолчанию выкл., доверенные папки и файлы, защищённый просмотр); мост
// MuJS на tests/fixtures/forms/forms-all.pdf — сценарий уровня документа выполняется при включении,
// AFNumber_Format форматирует 1234.5 (значение util.js MuPDF зафиксировано), AFSimple_Calculate пересчитывает
// сумму, своя проверка отклоняет «abc» с app.alert через фейковый ответчик, кнопка JavaScript показывает
// app.alert, лимит app.alert подряд (security-36), console.println и заглушки (app.response, launchURL,
// print, mailDoc), после записи и переоткрытия движок нужно включить снова, бесконечный цикл прерывается
// лимитом инструкций MuPDF.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/doc/document.h"
#include "core/doc/saver.h"
#include "core/forms/fields.h"
#include "core/forms/js_bridge.h"
#include "core/util/features.h"
#include "services/document_session.h"
#include "services/forms_ops.h"
#include "services/js_prompter.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "services/state_db.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
namespace forms = ot::forms;
using forms::JsMode;
using forms::JsState;
using ot::Document;

namespace {

fs::path fixture() { return ot::test::sourceDir() / "tests" / "fixtures" / "forms" / "forms-all.pdf"; }

class FakeHost : public forms::JsHost {
public:
    forms::AlertReply alert(const forms::AlertRequest& request) override {
        alerts.push_back(request);
        forms::AlertReply r;
        r.button = reply;
        return r;
    }
    void console(const std::string& line) override { lines.push_back(line); }
    void request(const forms::JsRequest& request) override { requests.push_back(request); }

    forms::AlertButton reply = forms::AlertButton::Ok;
    std::vector<forms::AlertRequest> alerts;
    std::vector<std::string> lines;
    std::vector<forms::JsRequest> requests;
};

struct Fixture {
    std::unique_ptr<Document> doc = Document::openOrThrow(fixture());
    std::shared_ptr<FakeHost> host = std::make_shared<FakeHost>();
    forms::JsBridge bridge{host};
    Fixture() { bridge.attach(*doc); }
};

}  // namespace

TEST(js_bridge, policy) {
    OT_FEATURE("security-37");
    OT_FEATURE("security-40");
    forms::JsPolicyInput in;
    in.hasScripts = true;
    EXPECT_EQ(forms::decide(in), JsState::Disabled);  // по умолчанию — выкл.
    in.trustedFile = true;
    EXPECT_EQ(forms::decide(in), JsState::Enabled);  // «Включить для этого файла»
    in.trustedFile = false;
    in.trustedFolder = true;
    EXPECT_EQ(forms::decide(in), JsState::Enabled);  // доверенная папка
    in.protectedView = true;
    EXPECT_EQ(forms::decide(in), JsState::ProtectedView);  // защищённый просмотр — всегда выкл.
    in.protectedView = false;
    in.mode = JsMode::Off;
    EXPECT_EQ(forms::decide(in), JsState::DisabledByUser);
    in.mode = JsMode::All;
    in.trustedFolder = false;
    EXPECT_EQ(forms::decide(in), JsState::Enabled);
    in.hasScripts = false;
    EXPECT_EQ(forms::decide(in), JsState::NoScripts);
    EXPECT_EQ(forms::jsModeFromName("off"), JsMode::Off);
    EXPECT_EQ(forms::jsModeFromName("all"), JsMode::All);
    EXPECT_EQ(forms::jsModeFromName("что-то"), JsMode::Trusted);
    EXPECT_STREQ(forms::jsModeName(JsMode::Trusted), "trusted");
}

TEST(js_bridge, document_level_script_runs_on_attach) {
    Fixture f;
    EXPECT_TRUE(forms::jsEnabled(*f.doc));
    EXPECT_TRUE(forms::hasJavaScript(*f.doc));
    EXPECT_EQ(forms::runScript(*f.doc, "ottiskFormLoaded"), "true");
    f.bridge.detach(*f.doc);
    EXPECT_FALSE(forms::jsEnabled(*f.doc));
    EXPECT_EQ(forms::runScript(*f.doc, "1 + 1"), "");
}

TEST(js_bridge, number_format) {
    OT_FEATURE("forms-31");
    Fixture f;
    const forms::SetResult r = forms::setValue(*f.doc, "a", "1234.5");
    ASSERT_TRUE(r.accepted) << r.error;
    EXPECT_EQ(forms::readField(*f.doc, "a")->value, "1234.5");
    // AFNumber_Format(2, 0, 0, 0, "", true) util.js MuPDF 1.28.5: запятая — разделитель тысяч, точка —
    // дробная.
    EXPECT_EQ(forms::formattedValue(*f.doc, "a"), "1,234.50");
    // Keystroke AFNumber_Keystroke не пускает буквы (и показывает app.alert).
    const forms::SetResult bad = forms::setValue(*f.doc, "b", "12x");
    EXPECT_FALSE(bad.accepted);
    EXPECT_EQ(forms::readField(*f.doc, "b")->value, "799.5");
}

TEST(js_bridge, calculate_updates_sum) {
    OT_FEATURE("forms-31");
    Fixture f;
    // Пересчёт при открытии: сумма та же.
    forms::recalculate(*f.doc);
    EXPECT_EQ(forms::formattedValue(*f.doc, "total"), "2,000.00");
    const forms::SetResult r = forms::setValue(*f.doc, "a", "100");
    ASSERT_TRUE(r.accepted) << r.error;
    EXPECT_EQ(forms::readField(*f.doc, "total")->value, "899.5");
    EXPECT_EQ(forms::formattedValue(*f.doc, "total"), "899.50");
    EXPECT_EQ(r.pages, std::vector<int>({0}));
}

TEST(js_bridge, validate_rejects_with_alert) {
    OT_FEATURE("forms-31");
    Fixture f;
    const forms::SetResult r = forms::setValue(*f.doc, "qty", "abc");
    EXPECT_FALSE(r.accepted);
    EXPECT_EQ(r.error, "rejected");
    EXPECT_EQ(forms::readField(*f.doc, "qty")->value, "3");
    ASSERT_EQ(f.host->alerts.size(), 1u);
    EXPECT_NE(f.host->alerts[0].message.find("abc"), std::string::npos) << f.host->alerts[0].message;
    EXPECT_NE(f.host->alerts[0].message.find("только цифры"), std::string::npos);
    // Верное значение проходит.
    EXPECT_TRUE(forms::setValue(*f.doc, "qty", "42").accepted);
    EXPECT_EQ(f.host->alerts.size(), 1u);
}

TEST(js_bridge, button_alert) {
    Fixture f;
    const forms::Field hello = *forms::readField(*f.doc, "hello");
    const forms::ClickResult r = forms::click(*f.doc, hello.widgets[0].page, hello.widgets[0].objnum);
    EXPECT_EQ(r.action.kind, forms::ActionKind::JavaScript);
    ASSERT_EQ(f.host->alerts.size(), 1u);
    EXPECT_EQ(f.host->alerts[0].message, "Привет из формы «Оттиска»");
    EXPECT_EQ(f.bridge.alertsShown(), 1);
    // Кнопка сброса: ResetForm выполняет ядро.
    ASSERT_TRUE(forms::setValue(*f.doc, "name", "Мария").accepted);
    const forms::Field reset = *forms::readField(*f.doc, "reset");
    const forms::ClickResult rr = forms::click(*f.doc, 0, reset.widgets[0].objnum);
    EXPECT_EQ(rr.action.kind, forms::ActionKind::ResetForm);
    EXPECT_EQ(forms::readField(*f.doc, "name")->value, "Ivan Petrov");
    // Отправка — не ядра: действие возвращается.
    const forms::Field submit = *forms::readField(*f.doc, "submit_http");
    const forms::ClickResult rs = forms::click(*f.doc, 0, submit.widgets[0].objnum);
    EXPECT_EQ(rs.action.kind, forms::ActionKind::SubmitForm);
    EXPECT_EQ(rs.action.target, "https://example.org/ottisk/submit");
}

TEST(js_bridge, alert_limit_per_user_action) {
    OT_FEATURE("forms-31");
    Fixture f;
    forms::beginUserAction(*f.doc);
    forms::runScript(*f.doc, "for (var i = 0; i < 5; ++i) app.alert('x' + i);");
    EXPECT_EQ(f.host->alerts.size(), 3u);  // security-36: не больше трёх подряд
    EXPECT_EQ(f.bridge.alertsSuppressed(), 2);
    forms::beginUserAction(*f.doc);
    forms::runScript(*f.doc, "app.alert('next')");
    ASSERT_EQ(f.host->alerts.size(), 4u);
    EXPECT_EQ(f.host->alerts.back().message, "next");
    // Ответ человека доходит до сценария.
    f.host->reply = forms::AlertButton::No;
    EXPECT_EQ(forms::runScript(*f.doc, "app.alert({cMsg: 'q', nType: 2})"), "3");
}

TEST(js_bridge, console_and_stubs) {
    Fixture f;
    forms::runScript(
        *f.doc,
        "console.println('hello'); app.response('question'); app.launchURL('https://example.org/x', "
        "true); this.print(); this.mailDoc(true, 'a@example.org'); this.saveAs('C:/x.pdf');");
    ASSERT_FALSE(f.host->lines.empty());
    EXPECT_EQ(f.host->lines[0], "hello");
    std::vector<forms::JsRequestKind> kinds;
    for (const forms::JsRequest& r : f.host->requests) {
        kinds.push_back(r.kind);
    }
    EXPECT_EQ(kinds, std::vector<forms::JsRequestKind>(
                         {forms::JsRequestKind::Response, forms::JsRequestKind::LaunchUrl,
                          forms::JsRequestKind::Print, forms::JsRequestKind::MailDoc,
                          forms::JsRequestKind::SaveAs}));
    EXPECT_EQ(f.host->requests[1].detail, "https://example.org/x");
    EXPECT_EQ(f.host->requests[3].detail, "a@example.org");
    // app.response — заглушка возвращает null (как «Отмена»).
    EXPECT_EQ(forms::runScript(*f.doc, "app.response('q')"), "null");
}

TEST(js_bridge, reattach_after_save) {
    const ot::test::TempDir dir;
    const fs::path file = dir.path() / "js.pdf";
    fs::copy_file(fixture(), file);
    auto doc = Document::openOrThrow(file);
    auto host = std::make_shared<FakeHost>();
    forms::JsBridge bridge(host);
    bridge.attach(*doc);
    ASSERT_TRUE(forms::setValue(*doc, "a", "10").accepted);
    ASSERT_TRUE(ot::Saver::save(*doc).ok());
    EXPECT_FALSE(forms::jsEnabled(*doc));  // Saver переоткрыл документ — движок MuPDF новый
    bridge.attach(*doc);
    EXPECT_TRUE(forms::jsEnabled(*doc));
    EXPECT_EQ(forms::readField(*doc, "total")->value, "809.5");
    forms::runScript(*doc, "app.alert('after')");
    ASSERT_EQ(host->alerts.size(), 1u);
}

TEST(js_bridge, runaway_script_is_stopped) {
    Fixture f;
    const std::string result = forms::runScript(*f.doc, "while (true) {}");
    EXPECT_NE(result.find("too long"), std::string::npos) << result;  // MuJS: «script ran too long»
    // Движок жив.
    EXPECT_EQ(forms::runScript(*f.doc, "2 + 2"), "4");
}

// ---------------------------------------------------------------- службы: политика, ответчик, сессия

namespace {

struct PolicyEnv {
    QTemporaryDir root;
    ot::Settings settings{root.filePath(QStringLiteral("settings.ini"))};
    ot::StateDb db{root.filePath(QStringLiteral("state.sqlite"))};
    PolicyEnv() { ot::test::qtApp(); }
    QString copy(const QString& name) {
        const QString path = QDir(root.path()).filePath(name);
        QFile::copy(QString::fromStdWString(fixture().wstring()), path);
        return path;
    }
};

}  // namespace

TEST(js_bridge, policy_enable_for_file_in_state_db) {
    OT_FEATURE("security-40");
    PolicyEnv env;
    ASSERT_TRUE(env.db.isOpen());
    const QString file = env.copy(QStringLiteral("анкета.pdf"));
    EXPECT_EQ(ot::js_policy::decide(file, false, true, env.settings, env.db), JsState::Disabled);
    ASSERT_TRUE(ot::js_policy::enableForFile(file, env.db));
    EXPECT_TRUE(ot::js_policy::enabledForFile(file, env.db));
    EXPECT_EQ(ot::js_policy::decide(file, false, true, env.settings, env.db), JsState::Enabled);
    // Защищённый просмотр сильнее.
    EXPECT_EQ(ot::js_policy::decide(file, true, true, env.settings, env.db), JsState::ProtectedView);
    // Другой файл (другое содержимое) — не доверен; та же копия под другим именем — доверена (отпечаток).
    const QString other = env.copy(QStringLiteral("копия.pdf"));
    EXPECT_EQ(ot::js_policy::decide(other, false, true, env.settings, env.db), JsState::Enabled);
    QFile f(other);
    ASSERT_TRUE(f.open(QIODevice::Append));
    f.write("\n% changed\n");
    f.close();
    EXPECT_EQ(ot::js_policy::decide(other, false, true, env.settings, env.db), JsState::Disabled);
    // Доверенная папка (security-37) и режимы настройки.
    env.settings.set(ot::settings::kTrustedFolders, QStringList{env.root.path()});
    EXPECT_EQ(ot::js_policy::decide(other, false, true, env.settings, env.db), JsState::Enabled);
    env.settings.set(ot::settings::kJavaScript, QStringLiteral("off"));
    EXPECT_EQ(ot::js_policy::decide(other, false, true, env.settings, env.db), JsState::DisabledByUser);
    env.settings.set(ot::settings::kJavaScript, QStringLiteral("all"));
    env.settings.set(ot::settings::kTrustedFolders, QStringList{});
    EXPECT_EQ(ot::js_policy::decide(QString(), false, true, env.settings, env.db), JsState::Enabled);
    EXPECT_EQ(ot::js_policy::decide(other, false, false, env.settings, env.db), JsState::NoScripts);
}

TEST(js_bridge, prompter_answers_from_gui_thread_and_times_out) {
    ot::test::qtApp();
    ot::JsPrompter prompter;
    forms::AlertRequest request;
    request.message = "Сохранить?";
    request.buttons = forms::AlertButtons::YesNo;
    // Без ответчика — ответ по умолчанию (Yes у «Да/Нет», как у закрытого окна Acrobat).
    EXPECT_EQ(prompter.alert(request).button, forms::AlertButton::Yes);
    std::vector<std::string> asked;
    prompter.setPrompt([&asked](const forms::AlertRequest& r) {
        asked.push_back(r.message);
        forms::AlertReply reply;
        reply.button = forms::AlertButton::No;
        return reply;
    });
    // Из другого потока — ответчик в потоке объекта (цикл событий крутит главный поток теста).
    forms::AlertReply fromThread;
    std::atomic<bool> done{false};
    std::thread worker([&] {
        fromThread = prompter.alert(request);
        done = true;
    });
    EXPECT_TRUE(ot::test::waitUntil([&] { return done.load(); }, 10000));
    worker.join();
    EXPECT_EQ(fromThread.button, forms::AlertButton::No);
    ASSERT_EQ(asked.size(), 1u);
    // Ответа нет (главный поток не крутит события) — по истечении срока ответ по умолчанию, окно не показано.
    prompter.setTimeout(200);
    forms::AlertReply late;
    std::thread silent([&] { late = prompter.alert(request); });
    silent.join();
    EXPECT_EQ(late.button, forms::AlertButton::Yes);
    QCoreApplication::processEvents();
    EXPECT_EQ(asked.size(), 1u);
}

TEST(js_bridge, session_keeps_javascript_after_save) {
    OT_FEATURE("forms-31");
    PolicyEnv env;
    const QString file = env.copy(QStringLiteral("форма.pdf"));
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(file));
    auto host = std::make_shared<FakeHost>();
    session.setJavaScript(host).waitForFinished();
    EXPECT_TRUE(session.javaScriptEnabled());
    forms::SetResult r = forms::setValue(session, QStringLiteral("qty"), QStringLiteral("abc")).result();
    EXPECT_FALSE(r.accepted);
    EXPECT_EQ(host->alerts.size(), 1u);
    ASSERT_TRUE(session.save().result().ok());
    EXPECT_TRUE(session.javaScriptEnabled());
    r = forms::setValue(session, QStringLiteral("a"), QStringLiteral("10")).result();
    ASSERT_TRUE(r.accepted);
    const forms::Form form = forms::read(session).result();
    EXPECT_EQ(form.find("total")->value, "809.5");  // вычисление идёт и после переоткрытия
    session.setJavaScript(nullptr).waitForFinished();
    EXPECT_FALSE(session.javaScriptEnabled());
    r = forms::setValue(session, QStringLiteral("qty"), QStringLiteral("xyz")).result();
    EXPECT_TRUE(r.accepted);  // без JavaScript проверки нет
    EXPECT_EQ(host->alerts.size(), 1u);
}
