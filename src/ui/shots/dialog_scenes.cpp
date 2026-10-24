// Сцены диалогов документа (план 2.5; эталон 3.1, 3.13, 6.3, 6.10): properties-description, properties-fonts,
// properties-initial-view — «Свойства документа» (Ctrl+D) на demo/report.pdf на своей вкладке; xmp-editor —
// «Дополнительные метаданные» поверх «Свойств» (категория «Дополнительно»); password-dialog — пароль после
// неверной попытки («Осталось попыток: 2»); recover-dialog — «Восстановить несохранённые изменения?» с двумя
// файлами на Главной; pdfa-bar — PDF/A-файл только для чтения с полосой «Включить редактирование» и «Только
// чтение» в строке состояния. Диалоги — окна верхнего уровня:
// для снимка встраиваются в главное окно (embedCentered). Кадр — когда диалог загружен и тайлы готовы.
#include <QTimer>

#include <filesystem>
#include <vector>

#include "ui/dialogs/password_dialog.h"
#include "ui/dialogs/properties_dialog.h"
#include "ui/dialogs/recover_dialog.h"
#include "ui/dialogs/xmp_editor.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"

namespace ot::ui {

namespace {

Scene propertiesScene(const char* name, PropertiesDialog::Tab tab, bool xmp = false) {
    Scene s;
    s.name = QLatin1String(name);
    s.make = withMainWindow([tab, xmp](MainWindow& w) {
        w.openFile(sceneFixture(QStringLiteral("demo/report.pdf")));
        QTimer::singleShot(0, &w, [&w, tab, xmp] {
            PropertiesDialog* d = w.showProperties(tab);
            if (d == nullptr) {
                return;
            }
            embedCentered(w, d);
            if (xmp) {
                QObject::connect(
                    d, &PropertiesDialog::loaded, d,
                    [&w, d] {
                        XmpEditor* x = d->showXmpEditor();
                        x->showCategory(XmpEditor::Advanced);
                        embedWindow(w, x);
                        x->move((w.width() - x->width()) / 2 + Theme::sizes().grid * 8,
                                (w.height() - x->height()) / 2 + Theme::sizes().grid * 8);
                        x->show();
                        x->raise();
                    },
                    Qt::SingleShotConnection);
            }
        });
    });
    s.ready = [tab, xmp](QWidget* w) {
        auto* window = static_cast<MainWindow*>(w);
        PropertiesDialog* d = window->propertiesDialog();
        if (d == nullptr || !d->isLoaded() || (tab == PropertiesDialog::Fonts && !d->fontsLoaded())) {
            return false;
        }
        if (xmp && (d->xmpEditor() == nullptr || !d->xmpEditor()->isVisible())) {
            return false;
        }
        return sceneDocumentReady(w);
    };
    return s;
}

}  // namespace

std::vector<Scene> dialogScenes() {
    std::vector<Scene> scenes;
    scenes.push_back(propertiesScene("properties-description", PropertiesDialog::Description));
    scenes.push_back(propertiesScene("properties-fonts", PropertiesDialog::Fonts));
    scenes.push_back(propertiesScene("properties-initial-view", PropertiesDialog::InitialViewTab));
    scenes.push_back(propertiesScene("xmp-editor", PropertiesDialog::Description, true));

    Scene password;
    password.name = QStringLiteral("password-dialog");
    password.make = withMainWindow([](MainWindow& w) {
        w.openFile(sceneFixture(QStringLiteral("demo/report.pdf")));
        QTimer::singleShot(0, &w, [&w] {
            auto* d = new PasswordDialog(
                MainWindow::tr("Договор аренды.pdf"), [](const QString&) { return false; }, &w);
            d->field()->setText(QStringLiteral("x"));
            d->submit();  // неверный — «Осталось попыток: 2»
            d->rememberBox()->setChecked(true);
            d->adjustSize();
            embedCentered(w, d);
        });
    });
    password.ready = [](QWidget* w) {
        return w->findChild<PasswordDialog*>() != nullptr && sceneDocumentReady(w);
    };
    scenes.push_back(password);

    Scene recover;
    recover.name = QStringLiteral("recover-dialog");
    recover.make = withMainWindow([](MainWindow& w) {
        QTimer::singleShot(0, &w, [&w] {
            std::vector<AutosaveEntry> entries(2);
            entries[0].path = MainWindow::tr("C:/Документы/Договор аренды.pdf").toStdWString();
            entries[0].savedAt = 1792828800;  // 2026-10-24 08:00 UTC
            entries[1].path = MainWindow::tr("C:/Документы/Счёт 17.pdf").toStdWString();
            entries[1].savedAt = 1792830600;
            auto* d = new RecoverDialog(std::move(entries), &w);
            d->adjustSize();
            embedCentered(w, d);
        });
    });
    recover.ready = [](QWidget* w) { return w->findChild<RecoverDialog*>() != nullptr; };
    scenes.push_back(recover);

    Scene pdfa;
    pdfa.name = QStringLiteral("pdfa-bar");
    pdfa.make = withMainWindow([](MainWindow& w) {
        w.openFile(sceneFixture(QStringLiteral("pick/bfo/pdfa2-6-8-bfo-t01-pass.pdf")));
    });
    pdfa.ready = sceneDocumentReady;
    scenes.push_back(pdfa);
    return scenes;
}

}  // namespace ot::ui
