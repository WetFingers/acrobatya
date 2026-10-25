// Ответы человека JavaScript документа (план 6.1, п. 1; эталон 3.6, 7.2; forms-31, security-36, security-37,
// security-40).
//
// JsPrompter — хозяин моста ядра (core/forms/js_bridge.h JsHost): app.alert приходит из потока документа,
// окно показывает поток интерфейса — вызов ответчика там же, где живёт объект (QMetaObject::invokeMethod), и
// ожидание QSemaphore не дольше 60 с (дальше — ответ по умолчанию, как у закрытого окна Acrobat: поток
// документа не висит, если интерфейс сам ждёт задачу документа); из потока интерфейса — ответчик сразу. Без
// ответчика — ответ по умолчанию и строка журнала. console.println — журнал и сигнал consoleLine (консоль
// JavaScript — 6.3); app.launchURL — сигнал launchRequested (окно спрашивает, как у ссылки, эталон 7.4);
// print, mailDoc, saveAs, app.response, submitForm — только журнал и сигнал requested (сценарий их не
// выполняет).
//
// js_policy — политика для файла: настройка trust/javascript, привилегированные папки trust/trustedFolders,
// «Включить для этого файла» — state.sqlite (trusted_files, scope «javascript», тот же отпечаток, что у
// защищённого просмотра: SHA-256 первых 64 КиБ и размер).
#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <mutex>

#include "core/forms/js_bridge.h"

namespace ot {

class Settings;
class StateDb;

class JsPrompter : public QObject, public forms::JsHost {
    Q_OBJECT
public:
    using Prompt = std::function<forms::AlertReply(const forms::AlertRequest&)>;
    static constexpr int kTimeoutMs = 60000;

    explicit JsPrompter(QObject* parent = nullptr);

    // Ответчик в потоке объекта (окно с кнопками); пусто — ответ по умолчанию без окна.
    void setPrompt(Prompt prompt);
    void setTimeout(int ms) noexcept { timeoutMs_ = ms; }
    int timeout() const noexcept { return timeoutMs_; }

    forms::AlertReply alert(const forms::AlertRequest& request) override;
    void console(const std::string& line) override;
    void request(const forms::JsRequest& request) override;

signals:
    void consoleLine(const QString& line);
    void launchRequested(const QString& url);
    void requested(int kind, const QString& detail);  // forms::JsRequestKind

private:
    Prompt prompt() const;

    mutable std::mutex mutex_;  // prompt_ ставит поток интерфейса, читает поток документа
    Prompt prompt_;
    int timeoutMs_ = kTimeoutMs;
};

namespace js_policy {

inline constexpr char kScope[] = "javascript";

// Решение для файла path (пусто — документ без файла: только режим «все»).
forms::JsState decide(const QString& path, bool protectedView, bool hasScripts);
forms::JsState decide(const QString& path, bool protectedView, bool hasScripts, const Settings& settings,
                      StateDb& db);
// «Включить для этого файла» (security-40): запомнить отпечаток файла. false — файла нет или база не открыта.
bool enableForFile(const QString& path);
bool enableForFile(const QString& path, StateDb& db);
bool enabledForFile(const QString& path, StateDb& db);

}  // namespace js_policy

}  // namespace ot
