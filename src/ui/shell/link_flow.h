// Ссылки и закладки наружу в интерфейсе (план 2.5b; эталон 3.1 «Навигация», 7.4–7.6; security-36,
// security-42): PageView сам переходит по внутренним (GoTo, Named) и шлёт linkActivated для любой ссылки;
// окно отдаёт действие сюда. Решение — services/link_policy.h (decideLink):
//   /URI сайта не из списка → «Предупреждение системы безопасности» (dialogs/link_confirm_dialog.h):
//   «Открыть» — один раз, «Разрешить для сайта» — запомнить и открыть, «Запретить» — запомнить и не
//   открывать; сайт разрешён — открыть без вопроса, запрещён — тост; открывает только ShellLauncher::openUrl
//   (в тестах FakeShellLauncher); /GoToR → «Документ пытается открыть другой файл» → вкладкой на странице
//   назначения; /Launch → никогда (тост и строка в журнал); /JavaScript, /SubmitForm, чужая схема — не
//   выполняются (тост); защищённый просмотр → ничего наружу (тост с подсказкой «Включить все функции»).
// Ответ диалога тесты подставляют (setConfirm); по умолчанию — exec().
#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

#include "core/links/links.h"
#include "services/link_policy.h"
#include "ui/dialogs/link_confirm_dialog.h"
#include "ui/widgets/tone.h"

namespace ot::ui {

class DocumentView;
class MainWindow;

class LinkFlow : public QObject {
    Q_OBJECT
public:
    using Confirm = std::function<LinkConfirmDialog::Choice(LinkConfirmDialog&)>;

    explicit LinkFlow(MainWindow& window);
    ~LinkFlow() override;

    void setConfirm(Confirm confirm) { confirm_ = std::move(confirm); }
    // Список сайтов (state.sqlite): создаётся при первой ссылке наружу.
    LinkTrust& trust();

    // Действие ссылки или закладки вкладки view (внутренний переход вид уже сделал). Возвращает решение.
    LinkDecision follow(DocumentView& view, const links::LinkAction& action);

    // Последнее сообщение тостом (тесты).
    QString lastMessage() const { return message_; }

signals:
    void urlOpened(const QString& url);

private:
    LinkConfirmDialog::Choice ask(LinkConfirmDialog::Kind kind, const QString& target, const QString& host);
    void openUrl(const QUrl& url);
    void notify(const QString& text, Tone tone);

    MainWindow& window_;
    std::unique_ptr<LinkTrust> trust_;
    Confirm confirm_;
    QString message_;
};

}  // namespace ot::ui
