#include "ui/shell/link_flow.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <spdlog/spdlog.h>

#include "services/shell_launcher.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/widgets/toast.h"

namespace ot::ui {

LinkFlow::LinkFlow(MainWindow& window) : QObject(&window), window_(window) {}

LinkFlow::~LinkFlow() = default;

LinkTrust& LinkFlow::trust() {
    if (!trust_) {
        trust_ = std::make_unique<LinkTrust>();
    }
    return *trust_;
}

LinkConfirmDialog::Choice LinkFlow::ask(LinkConfirmDialog::Kind kind, const QString& target,
                                        const QString& host) {
    LinkConfirmDialog dialog(kind, target, host, &window_);
    if (confirm_) {
        return confirm_(dialog);
    }
    dialog.exec();
    return dialog.choice();
}

void LinkFlow::notify(const QString& text, Tone tone) {
    message_ = text;
    Toast::notify(&window_, text, tone);
}

void LinkFlow::openUrl(const QUrl& url) {
    if (ShellLauncher::instance().openUrl(url)) {
        emit urlOpened(url.toString());
        return;
    }
    notify(tr("Не удалось открыть ссылку."), Tone::Danger);
}

LinkDecision LinkFlow::follow(DocumentView& view, const links::LinkAction& action) {
    const LinkDecision decision = decideLink(action, view.isProtectedView(), trust());
    const QUrl url = linkUrl(action);
    const QString host = LinkTrust::hostKey(url);
    switch (decision) {
        case LinkDecision::Navigate:
            break;
        case LinkDecision::OpenUrl:
            openUrl(url);
            break;
        case LinkDecision::BlockedUrl:
            notify(host.isEmpty() ? tr("Ссылки этого вида запрещены в Диспетчере доверия.")
                                  : tr("Сайт %1 запрещён в Диспетчере доверия.").arg(host),
                   Tone::Warn);
            break;
        case LinkDecision::AskUrl: {
            const QString shown = url.toDisplayString();
            switch (ask(LinkConfirmDialog::Kind::Url, shown, host)) {
                case LinkConfirmDialog::Choice::AllowSite:
                    trust().setRule(host, HostRule::Allow);
                    openUrl(url);
                    break;
                case LinkConfirmDialog::Choice::Open:
                    openUrl(url);
                    break;
                case LinkConfirmDialog::Choice::Block:
                    trust().setRule(host, HostRule::Block);
                    break;
                case LinkConfirmDialog::Choice::Cancel:
                    break;
            }
            break;
        }
        case LinkDecision::AskFile: {
            const QString path = remoteFilePath(action, view.path());
            if (path.isEmpty()) {
                notify(tr("Ссылка не указывает файл."), Tone::Warn);
                break;
            }
            if (ask(LinkConfirmDialog::Kind::File, QDir::toNativeSeparators(path), {}) !=
                LinkConfirmDialog::Choice::Open) {
                break;
            }
            if (!QFileInfo(path).isFile()) {
                notify(tr("Файл «%1» не найден.").arg(QFileInfo(path).fileName()), Tone::Danger);
                break;
            }
            window_.openFile(path, action.page);
            break;
        }
        case LinkDecision::Forbidden:
            spdlog::warn("link: /Launch {} refused", action.file);
            notify(tr("Документ пытается запустить программу «%1». Запуск программ из документов запрещён.")
                       .arg(QString::fromStdString(action.file)),
                   Tone::Danger);
            break;
        case LinkDecision::Unsupported:
            notify(tr("Это действие ссылки не выполняется."), Tone::Info);
            break;
        case LinkDecision::ProtectedView:
            notify(tr("Защищённый просмотр: ссылки на сайты и файлы отключены. Чтобы открыть ссылку, нажмите "
                      "«Включить все функции»."),
                   Tone::Warn);
            break;
    }
    return decision;
}

}  // namespace ot::ui
