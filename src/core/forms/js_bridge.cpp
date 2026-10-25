// JavaScript документа: политика и мост MuJS (см. js_bridge.h).
#include "core/forms/js_bridge.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>

#include <spdlog/spdlog.h>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/util/paths.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace ot::forms {

// ---------------------------------------------------------------- политика

JsMode jsModeFromName(std::string_view name) noexcept {
    if (name == "off") {
        return JsMode::Off;
    }
    if (name == "all") {
        return JsMode::All;
    }
    return JsMode::Trusted;
}

const char* jsModeName(JsMode mode) noexcept {
    switch (mode) {
        case JsMode::Off:
            return "off";
        case JsMode::All:
            return "all";
        case JsMode::Trusted:
            break;
    }
    return "trusted";
}

const char* jsStateName(JsState state) noexcept {
    switch (state) {
        case JsState::Enabled:
            return "enabled";
        case JsState::Disabled:
            return "disabled";
        case JsState::ProtectedView:
            return "protected-view";
        case JsState::DisabledByUser:
            return "disabled-by-user";
        case JsState::NoScripts:
            break;
    }
    return "no-scripts";
}

JsState decide(const JsPolicyInput& in) noexcept {
    if (!in.hasScripts) {
        return JsState::NoScripts;
    }
    if (in.protectedView) {
        return JsState::ProtectedView;
    }
    if (in.mode == JsMode::Off) {
        return JsState::DisabledByUser;
    }
    if (in.mode == JsMode::All || in.trustedFolder || in.trustedFile) {
        return JsState::Enabled;
    }
    return JsState::Disabled;
}

AlertReply defaultReply(const AlertRequest& request) noexcept {
    AlertReply r;
    switch (request.buttons) {
        case AlertButtons::Ok:
            r.button = AlertButton::Ok;
            break;
        case AlertButtons::OkCancel:
        case AlertButtons::YesNoCancel:
            r.button = AlertButton::Cancel;
            break;
        case AlertButtons::YesNo:
            r.button = AlertButton::Yes;
            break;
    }
    r.checked = request.checked;
    return r;
}

const char* jsRequestName(JsRequestKind kind) noexcept {
    switch (kind) {
        case JsRequestKind::LaunchUrl:
            return "app.launchURL";
        case JsRequestKind::Print:
            return "print";
        case JsRequestKind::MailDoc:
            return "mailDoc";
        case JsRequestKind::ExecMenuItem:
            return "app.execMenuItem";
        case JsRequestKind::Response:
            return "app.response";
        case JsRequestKind::SaveAs:
            return "saveAs";
        case JsRequestKind::SubmitForm:
            return "submitForm";
        case JsRequestKind::ImportData:
            break;
    }
    return "importDataObject";
}

// ---------------------------------------------------------------- мост

struct JsBridge::State {
    static constexpr std::uint32_t kMagic = 0x4A534252;  // «JSBR»
    std::uint32_t magic = kMagic;
    std::shared_ptr<JsHost> host;
    int alertLimit = 3;
    std::atomic<int> streak{0};
    std::atomic<int> shown{0};
    std::atomic<int> suppressed{0};
    pdf_js_console console{};
    std::string line;  // console.println: строка копится до следующего «\n»
    bool hasLine = false;
};

namespace js_impl {

// Заглушки сценариев, которых нет в pdf-js.c MuPDF 1.28.5: вызов — пункт меню с меткой (приходит событием
// PDF_DOCUMENT_EVENT_EXEC_MENU_ITEM в onEvent), ответ — null.
constexpr const char* kStubPrefix = "AcroBatya:stub:";
constexpr const char* kStubs =
    "app.response = function () { app.execMenuItem('AcroBatya:stub:response'); return null; };\n"
    "global.saveAs = function () { app.execMenuItem('AcroBatya:stub:saveAs'); return null; };\n"
    "global.submitForm = function () { app.execMenuItem('AcroBatya:stub:submitForm'); return null; };\n"
    "global.importDataObject = function () { app.execMenuItem('AcroBatya:stub:importData'); return null; "
    "};\n";

void flushLine(JsBridge::State& s) noexcept {
    if (!s.hasLine) {
        return;
    }
    s.hasLine = false;
    try {
        spdlog::info("js console: {}", s.line);
        s.host->console(s.line);
    } catch (const std::exception& e) {
        spdlog::warn("js console: host failed: {}", e.what());
    }
    s.line.clear();
}

void consoleWrite(void* user, const char* message) {
    auto* s = static_cast<JsBridge::State*>(user);
    if (s == nullptr || message == nullptr) {
        return;
    }
    try {
        if (std::strcmp(message, "\n") == 0) {
            flushLine(*s);
            s->hasLine = true;
        } else {
            s->line += message;
            s->hasLine = true;
        }
    } catch (...) {  // NOLINT(bugprone-empty-catch): колбэк C — исключение наружу не выпускать
    }
}

void consoleClear(void* user) { (void)user; }

AlertIcon iconOf(int t) {
    switch (t) {
        case PDF_ALERT_ICON_WARNING:
            return AlertIcon::Warning;
        case PDF_ALERT_ICON_QUESTION:
            return AlertIcon::Question;
        case PDF_ALERT_ICON_STATUS:
            return AlertIcon::Status;
        default:
            return AlertIcon::Error;
    }
}

AlertButtons buttonsOf(int t) {
    switch (t) {
        case PDF_ALERT_BUTTON_GROUP_OK_CANCEL:
            return AlertButtons::OkCancel;
        case PDF_ALERT_BUTTON_GROUP_YES_NO:
            return AlertButtons::YesNo;
        case PDF_ALERT_BUTTON_GROUP_YES_NO_CANCEL:
            return AlertButtons::YesNoCancel;
        default:
            return AlertButtons::Ok;
    }
}

int pressedOf(AlertButton b) {
    switch (b) {
        case AlertButton::Ok:
            return PDF_ALERT_BUTTON_OK;
        case AlertButton::Cancel:
            return PDF_ALERT_BUTTON_CANCEL;
        case AlertButton::No:
            return PDF_ALERT_BUTTON_NO;
        case AlertButton::Yes:
            return PDF_ALERT_BUTTON_YES;
        case AlertButton::None:
            break;
    }
    return PDF_ALERT_BUTTON_NONE;
}

void request(JsBridge::State& s, JsRequestKind kind, std::string detail) {
    spdlog::info("js: {} is not performed ({})", jsRequestName(kind), detail);
    try {
        s.host->request(JsRequest{kind, std::move(detail)});
    } catch (const std::exception& e) {
        spdlog::warn("js: host request failed: {}", e.what());
    }
}

void alert(JsBridge::State& s, pdf_alert_event* evt) {
    flushLine(s);
    AlertRequest r;
    r.message = evt->message != nullptr ? evt->message : "";
    r.title = evt->title != nullptr ? evt->title : "";
    r.icon = iconOf(evt->icon_type);
    r.buttons = buttonsOf(evt->button_group_type);
    r.checkBox = evt->has_check_box != 0;
    r.checkBoxMessage = evt->check_box_message != nullptr ? evt->check_box_message : "";
    r.checked = evt->initially_checked != 0;
    if (s.streak.fetch_add(1) >= s.alertLimit) {
        // security-36: сценарий не держит человека окнами без конца — ответ по умолчанию (как закрытое окно).
        s.suppressed.fetch_add(1);
        spdlog::warn("js: app.alert suppressed (more than {} in a row): {}", s.alertLimit, r.message);
        return;
    }
    s.shown.fetch_add(1);
    AlertReply reply = defaultReply(r);
    try {
        reply = s.host->alert(r);
    } catch (const std::exception& e) {
        spdlog::warn("js: app.alert host failed: {}", e.what());
    }
    evt->button_pressed = pressedOf(reply.button);
    evt->finally_checked = reply.checked ? 1 : 0;
}

void onEvent(fz_context* ctx, pdf_document* doc, pdf_doc_event* evt, void* data) {
    (void)doc;
    auto* s = static_cast<JsBridge::State*>(data);
    if (s == nullptr || s->magic != JsBridge::State::kMagic || evt == nullptr) {
        return;
    }
    try {
        switch (evt->type) {
            case PDF_DOCUMENT_EVENT_ALERT:
                alert(*s, pdf_access_alert_event(ctx, evt));
                break;
            case PDF_DOCUMENT_EVENT_PRINT:
                request(*s, JsRequestKind::Print, {});
                break;
            case PDF_DOCUMENT_EVENT_LAUNCH_URL: {
                const pdf_launch_url_event* e = pdf_access_launch_url_event(ctx, evt);
                request(*s, JsRequestKind::LaunchUrl, e != nullptr && e->url != nullptr ? e->url : "");
                break;
            }
            case PDF_DOCUMENT_EVENT_MAIL_DOC: {
                const pdf_mail_doc_event* e = pdf_access_mail_doc_event(ctx, evt);
                request(*s, JsRequestKind::MailDoc, e != nullptr && e->to != nullptr ? e->to : "");
                break;
            }
            case PDF_DOCUMENT_EVENT_EXEC_MENU_ITEM: {
                const char* item = pdf_access_exec_menu_item_event(ctx, evt);
                const std::string name = item != nullptr ? item : "";
                const std::string prefix = kStubPrefix;
                if (name.rfind(prefix, 0) == 0) {
                    const std::string stub = name.substr(prefix.size());
                    const JsRequestKind kind = stub == "response"     ? JsRequestKind::Response
                                               : stub == "saveAs"     ? JsRequestKind::SaveAs
                                               : stub == "submitForm" ? JsRequestKind::SubmitForm
                                                                      : JsRequestKind::ImportData;
                    request(*s, kind, {});
                } else {
                    request(*s, JsRequestKind::ExecMenuItem, name);
                }
                break;
            }
            default:
                break;
        }
    } catch (...) {  // NOLINT(bugprone-empty-catch): колбэк C — исключение наружу не выпускать
    }
}

JsBridge::State* stateOf(const Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr || pdf->event_cb != &onEvent) {
        return nullptr;
    }
    auto* s = static_cast<JsBridge::State*>(pdf->event_cb_data);
    return s != nullptr && s->magic == JsBridge::State::kMagic ? s : nullptr;
}

}  // namespace js_impl

JsBridge::JsBridge(std::shared_ptr<JsHost> host, int alertLimit) : state_(std::make_unique<State>()) {
    if (!host) {
        throw std::invalid_argument("JsBridge: host is required");
    }
    state_->host = std::move(host);
    state_->alertLimit = alertLimit;
    state_->console.write = &js_impl::consoleWrite;
    state_->console.clear = &js_impl::consoleClear;
}

JsBridge::~JsBridge() = default;

void JsBridge::attach(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return;
    }
    fz_context* ctx = doc.context().get();
    State* s = state_.get();
    const char* stubs = js_impl::kStubs;
    // Сценарии уровня документа выполняются при pdf_enable_js — колбэки ставятся до него.
    mu::call(ctx, [&] { pdf_set_doc_event_callback(ctx, pdf, &js_impl::onEvent, nullptr, s); });
    Journal::Operation op(doc, "Enable JavaScript");
    mu::call(ctx, [&] {
        pdf_enable_js(ctx, pdf);
        pdf_js_set_console(ctx, pdf, &s->console, s);
        pdf_js_execute(pdf->js, "AcroBatya stubs", stubs, nullptr);
    });
    op.commit();
    js_impl::flushLine(*s);
    spdlog::info("js: enabled for {}", paths::toUtf8(doc.path().filename()));
}

void JsBridge::detach(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return;
    }
    js_impl::flushLine(*state_);
    fz_context* ctx = doc.context().get();
    mu::call(ctx, [&] {
        pdf_disable_js(ctx, pdf);
        if (pdf->event_cb == &js_impl::onEvent) {
            pdf_set_doc_event_callback(ctx, pdf, nullptr, nullptr, nullptr);
        }
    });
}

JsHost& JsBridge::host() const noexcept { return *state_->host; }

int JsBridge::alertLimit() const noexcept { return state_->alertLimit; }

int JsBridge::alertsShown() const noexcept { return state_->shown.load(); }

int JsBridge::alertsSuppressed() const noexcept { return state_->suppressed.load(); }

bool jsEnabled(const Document& doc) {
    pdf_document* pdf = doc.pdf();
    return pdf != nullptr && pdf->js != nullptr;
}

void beginUserAction(Document& doc) {
    if (JsBridge::State* s = js_impl::stateOf(doc)) {
        js_impl::flushLine(*s);
        s->streak.store(0);
    }
}

std::string runScript(Document& doc, std::string_view code) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr || pdf->js == nullptr) {
        return {};
    }
    fz_context* ctx = doc.context().get();
    const std::string source(code);
    const char* src = source.c_str();
    char* result = nullptr;
    Journal::Operation op(doc, "Run script");
    mu::call(ctx, [&] { pdf_js_execute(pdf->js, "console", src, &result); });
    op.commit();
    if (JsBridge::State* s = js_impl::stateOf(doc)) {
        js_impl::flushLine(*s);
    }
    std::string out = result != nullptr ? result : "";
    fz_free(ctx, result);
    return out;
}

}  // namespace ot::forms
