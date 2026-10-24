// Ссылки из PDF наружу (план 2.5b; эталон 7.4, 7.6; security-36, security-42): что делать с действием ссылки
// или закладки. Внутренние (GoTo, Named) — переход в виде; /URI (http, https, mailto) — по списку сайтов
// Диспетчера доверия: разрешён — открыть, запрещён — нет, не в списке — по настройке trust/internetAccess
// (по умолчанию — спросить: «Открыть ссылку https://…? [Открыть] [Разрешить для сайта] [Запретить]»); прочие
// схемы (javascript:, file:, ms-…) — не поддерживаются; /GoToR — «Открыть файл …?»; /Launch — запрещено
// всегда; в защищённом просмотре внешнее не открывается вовсе. Открытие — только ShellLauncher::openUrl (в
// тестах FakeShellLauncher); подтверждение — дело ui (dialogs/link_confirm_dialog).
//
// Список сайтов — state.sqlite (link_hosts: хост → разрешить/запретить); его же покажет Диспетчер
// доверия 7.6. Поток: GUI (база — под StateDb::mutex()).
#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QUrl>

#include <memory>

#include "core/links/links.h"

namespace ot {

class Settings;
class StateDb;

enum class HostRule { Ask = 0, Allow = 1, Block = 2 };

class LinkTrust {
public:
    LinkTrust();  // StateDb::shared(), settings.ini
    LinkTrust(StateDb& db, const Settings& settings);
    ~LinkTrust();
    LinkTrust(const LinkTrust&) = delete;
    LinkTrust& operator=(const LinkTrust&) = delete;

    // Ключ сайта: хост в нижнем регистре (пусто — у адреса нет хоста, как у mailto:).
    static QString hostKey(const QUrl& url);

    // Правило для сайта: из списка, иначе — по trust/internetAccess (ask / allowAll / blockAll).
    HostRule rule(const QString& host) const;
    // Только из списка (Ask — сайта в списке нет).
    HostRule listedRule(const QString& host) const;
    // Разрешить / запретить сайт; Ask — убрать из списка. Пустой хост не запоминается.
    void setRule(const QString& host, HostRule rule);
    // Список: хост → правило, по алфавиту.
    QList<QPair<QString, HostRule>> hosts() const;

private:
    StateDb& db_;
    std::unique_ptr<Settings> own_;
    const Settings* settings_ = nullptr;
};

enum class LinkDecision {
    Navigate,       // переход в этом документе (GoTo, Named)
    AskUrl,         // спросить «Открыть ссылку …?»
    OpenUrl,        // сайт разрешён — открыть без вопроса
    BlockedUrl,     // сайт запрещён (список или «запретить все»)
    AskFile,        // /GoToR — «Открыть файл …?»
    Forbidden,      // /Launch — никогда
    Unsupported,    // /JavaScript, /SubmitForm, чужая схема адреса — не выполняется
    ProtectedView,  // защищённый просмотр: внешние ссылки отключены
};

// Адрес /URI действия (QUrl по байтам MuPDF).
QUrl linkUrl(const links::LinkAction& action);
// Схема, которую программа открывает наружу: http, https, mailto.
bool isWebUrl(const QUrl& url);
LinkDecision decideLink(const links::LinkAction& action, bool protectedView, const LinkTrust& trust);
// Файл /GoToR: относительный — от папки документа, «/C/Docs/a.pdf» (путь PDF) — «C:/Docs/a.pdf».
QString remoteFilePath(const links::LinkAction& action, const QString& documentPath);

}  // namespace ot
