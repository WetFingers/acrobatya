// Сцены доверия (план 2.5b; эталон 6.3, 6.6, 7.4, 7.5): protected-view — отчёт, «скачанный из Интернета»
// (копия demo/report.pdf во временной папке процесса с потоком Zone.Identifier ZoneId=3), с жёлтой полосой
// «Защищённый просмотр … [Включить все функции]»; link-confirm — «Предупреждение системы безопасности» перед
// ссылкой https://example.org на view/links.pdf. Диалог для снимка встраивается в окно (embedCentered).
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>
#include <vector>

#include "services/protected_view.h"
#include "ui/dialogs/link_confirm_dialog.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"

namespace ot::ui {

namespace {

// Копия фикстуры с пометкой «из Интернета» — в папке, которая живёт до конца процесса.
QString internetCopy(const QString& fixture, const QString& name) {
    static const std::unique_ptr<QTemporaryDir> dir = std::make_unique<QTemporaryDir>();
    const QString target = QDir(dir->path()).filePath(name);
    if (!QFile::exists(target)) {
        QFile in(sceneFixture(fixture));
        QFile out(target);
        if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
            return {};
        }
        out.write(in.readAll());
        // Свой отпечаток: «Включить все функции» прошлых прогонов не в счёт.
        out.write("\n% protected-view scene\n");
        out.close();
        protected_view::writeZoneIdentifier(target, 3, QStringLiteral("https://example.org/report.pdf"));
    }
    return target;
}

}  // namespace

std::vector<Scene> trustScenes() {
    std::vector<Scene> scenes;

    Scene pv;
    pv.name = QStringLiteral("protected-view");
    pv.make = withMainWindow([](MainWindow& w) {
        w.openFile(internetCopy(QStringLiteral("demo/report.pdf"), MainWindow::tr("Отчёт за квартал.pdf")));
    });
    pv.ready = sceneDocumentReady;
    scenes.push_back(pv);

    Scene link;
    link.name = QStringLiteral("link-confirm");
    link.make = withMainWindow([](MainWindow& w) {
        w.openFile(sceneFixture(QStringLiteral("view/links.pdf")));
        QTimer::singleShot(0, &w, [&w] {
            auto* d = new LinkConfirmDialog(LinkConfirmDialog::Kind::Url,
                                            QStringLiteral("https://example.org/akrobatya"),
                                            QStringLiteral("example.org"), &w);
            d->adjustSize();
            embedCentered(w, d);
        });
    });
    link.ready = [](QWidget* w) {
        return w->findChild<LinkConfirmDialog*>() != nullptr && sceneDocumentReady(w);
    };
    scenes.push_back(link);
    return scenes;
}

}  // namespace ot::ui
