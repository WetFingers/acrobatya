// JavaScript документа (план 6.1, п. 1; эталон 3.6, 7.2; forms-31, security-36, security-37, security-40):
// политика включения и мост MuJS MuPDF.
//
// Политика (эталон 7.2): выключен по умолчанию для всех файлов, кроме «доверенных папок» (Диспетчер доверия)
// и файлов, для которых человек нажал «Включить для этого файла» (state.sqlite по отпечатку — services);
// защищённый просмотр — всегда выключен; настройка «JavaScript» — выкл. / доверенные / все. При выключенном
// формы заполняются, вычисления и форматы не идут.
//
// Мост (JsBridge::attach): pdf_enable_js документа, консоль (console.println → JsHost::console), события
// документа pdf_set_doc_event_callback — app.alert (PDF_DOCUMENT_EVENT_ALERT; ответ — button_pressed) идёт к
// JsHost::alert в потоке документа, не больше alertLimit подряд за одно действие человека (security-36:
// «запрет тихой печати» — дальше ответ по умолчанию и строка журнала); app.launchURL, print, mailDoc,
// execMenuItem — JsHost::request (сами ничего не делают: адрес — подтверждение ссылки в ui, остальное —
// заглушки с записью). Своё в MuJS — заглушки app.response, saveAs, submitForm, importDataObject (в pdf-js.c
// MuPDF 1.28.5 их нет; вызов пишет строку журнала и возвращает null). MuPDF ограничивает сценарий 10 млн
// инструкций и 100 МБ (js_setlimit в pdf_js_execute) — бесконечный цикл прерывается ошибкой сценария;
// отдельного таймаута нет, сценарий идёт только в потоке документа и интерфейс не блокирует.
//
// Состояние моста (хозяин, счётчики) принадлежит JsBridge, а не документу: MuPDF не освобождает данные
// колбэка при закрытии. После записи и переоткрытия (Saver) движок MuPDF создаётся заново — attach ещё раз.
// Поток — поток документа.
#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace ot {
class Document;
}

namespace ot::forms {

// ---------------------------------------------------------------- политика

enum class JsMode { Off, Trusted, All };  // настройка trust/javascript: off | trusted | all
JsMode jsModeFromName(std::string_view name) noexcept;
const char* jsModeName(JsMode mode) noexcept;

// Почему JavaScript документа включён или нет.
enum class JsState {
    Enabled,         // включён: файл в доверенной папке, «Включить для этого файла» или режим «все»
    Disabled,        // выключен по умолчанию — полоса «JavaScript отключён [Включить для этого файла]»
    ProtectedView,   // защищённый просмотр — включить нельзя, пока не «Включить все функции»
    DisabledByUser,  // режим «выкл.» — полосы с кнопкой нет
    NoScripts,       // сценариев в документе нет — полоса не нужна
};
const char* jsStateName(JsState state) noexcept;

struct JsPolicyInput {
    JsMode mode = JsMode::Trusted;
    bool hasScripts = false;
    bool protectedView = false;
    bool trustedFolder = false;  // файл в привилегированной папке (trust/trustedFolders)
    bool trustedFile = false;    // «Включить для этого файла» (state.sqlite, scope javascript)
};
JsState decide(const JsPolicyInput& input) noexcept;
inline bool enabledBy(JsState state) noexcept { return state == JsState::Enabled; }

// ---------------------------------------------------------------- хозяин

enum class AlertIcon { Error, Warning, Question, Status };
enum class AlertButtons { Ok, OkCancel, YesNo, YesNoCancel };
enum class AlertButton { None, Ok, Cancel, No, Yes };

struct AlertRequest {
    std::string message;
    std::string title;
    AlertIcon icon = AlertIcon::Error;
    AlertButtons buttons = AlertButtons::Ok;
    bool checkBox = false;
    std::string checkBoxMessage;
    bool checked = false;
};

struct AlertReply {
    AlertButton button = AlertButton::Ok;
    bool checked = false;
};
// Ответ, который Acrobat даёт закрытому окну (MuPDF: OK, Cancel, Yes, Cancel).
AlertReply defaultReply(const AlertRequest& request) noexcept;

// Что сценарий попросил сделать вне документа: программа это молча не делает (эталон 7.2).
enum class JsRequestKind {
    LaunchUrl,
    Print,
    MailDoc,
    ExecMenuItem,
    Response,
    SaveAs,
    SubmitForm,
    ImportData
};
const char* jsRequestName(JsRequestKind kind) noexcept;
struct JsRequest {
    JsRequestKind kind = JsRequestKind::ExecMenuItem;
    std::string detail;  // адрес, пункт меню, получатель
};

class JsHost {
public:
    virtual ~JsHost() = default;
    // app.alert — в потоке документа; может ждать человека (services/js_prompter.h: до 60 с).
    virtual AlertReply alert(const AlertRequest& request) = 0;
    // console.println — строка (без перевода строки в конце).
    virtual void console(const std::string& line) { (void)line; }
    virtual void request(const JsRequest& request) { (void)request; }
};

// ---------------------------------------------------------------- мост

class JsBridge {
public:
    explicit JsBridge(std::shared_ptr<JsHost> host, int alertLimit = 3);
    ~JsBridge();
    JsBridge(const JsBridge&) = delete;
    JsBridge& operator=(const JsBridge&) = delete;

    // Включить JavaScript документа (сценарии уровня документа выполняются сразу — операция журнала MuPDF
    // «Document level Javascript», если они что-то меняют). Повторно и после переоткрытия — то же.
    void attach(Document& doc);
    // Выключить: pdf_disable_js и колбэки документа сняты.
    void detach(Document& doc);

    JsHost& host() const noexcept;
    int alertLimit() const noexcept;
    // Окон app.alert показано / подавлено лимитом с создания моста.
    int alertsShown() const noexcept;
    int alertsSuppressed() const noexcept;

    struct State;

private:
    std::unique_ptr<State> state_;
};

// JavaScript документа включён (движок MuPDF есть).
bool jsEnabled(const Document& doc);
// Новое действие человека (ввод в поле, щелчок по кнопке): счётчик app.alert подряд — с нуля. Без моста —
// ничего.
void beginUserAction(Document& doc);
// Выполнить сценарий в документе (тесты; консоль JavaScript — 6.3): результат (repr) или текст ошибки;
// JavaScript выключен — пусто. Операция журнала MuPDF «Javascript Event», если сценарий меняет документ.
std::string runScript(Document& doc, std::string_view code);

}  // namespace ot::forms
