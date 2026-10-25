// Сцены заполнения формы (план 6.1, п. 2; эталон 3.6, 6.1 «фиолетовая полоса», 7.2): на
// tests/fixtures/forms/forms-all.pdf — form-fill (полосы формы и JavaScript, редактор поверх поля «ФИО» с
// кириллицей), form-highlight (подсветка полей: обязательных — нет, «Итого» только для чтения — серое, фокус
// на флажке), form-combo-open (список «Город» раскрыт), form-js-bar (полоса «JavaScript отключён [Включить
// для этого файла]»), form-js-alert (окно app.alert сценария); на tests/fixtures/forms/xfa-stub.pdf —
// form-xfa-bar. Тёмный двойник — form-fill-dark.
#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QLineEdit>
#include <QTimer>

#include <algorithm>
#include <functional>

#include "core/forms/js_bridge.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"
#include "ui/tools/forms/field_editors.h"
#include "ui/tools/forms/form_fill_tool.h"
#include "ui/tools/forms/js_alert_dialog.h"
#include "ui/view/page_view.h"

namespace ot::ui {

namespace form_scene {  // не анонимное: unity-сборка сцен

constexpr const char* kDoneProperty = "formSceneDone";

bool formReady(QWidget* w) { return w->property(kDoneProperty).toBool() && sceneDocumentReady(w); }

// Окно с формой file; then — когда снимок формы пришёл (поля, полосы).
std::function<QWidget*()> formScene(const char* file, std::function<void(MainWindow&, DocumentView&)> then) {
    return withMainWindow([file, then](MainWindow& w) {
        w.setProperty(kDoneProperty, false);
        w.openFile(sceneFixture(QLatin1String(file)));
        QTimer::singleShot(0, &w, [&w, then] {
            DocumentView* doc = w.currentDocument();
            if (doc == nullptr) {
                return;
            }
            doc->view().setZoomPercent(100);
            doc->view().goToPage(0);
            // Вид устраивается после показа (масштаб по умолчанию, раскладка) — сцена доводится чуть позже,
            // кадр готов ещё позже (всплывающее встраивается по месту поля).
            auto run = [&w, then] {
                QTimer::singleShot(300, &w, [&w, then] {
                    if (DocumentView* d = w.currentDocument()) {
                        then(w, *d);
                    }
                    QTimer::singleShot(300, &w, [&w] { w.setProperty(kDoneProperty, true); });
                });
            };
            if (doc->forms().isLoaded()) {
                run();
            } else {
                QObject::connect(&doc->forms(), &FormFillTool::loaded, &w, run, Qt::SingleShotConnection);
            }
        });
    });
}

Scene scene(const char* name, const char* file, std::function<void(MainWindow&, DocumentView&)> then) {
    Scene s;
    s.name = QLatin1String(name);
    s.make = formScene(file, std::move(then));
    s.ready = formReady;
    return s;
}

}  // namespace form_scene

std::vector<Scene> formScenes() {
    using form_scene::scene;
    std::vector<Scene> scenes;
    const auto fill = [](MainWindow&, DocumentView& doc) {
        doc.forms().setHighlight(false);
        if (doc.forms().focusField(QStringLiteral("name"))) {
            if (auto* line = qobject_cast<QLineEdit*>(doc.forms().editor()->input())) {
                line->setText(QCoreApplication::translate("ot::ui::FormScenes", "Мария Иванова"));
            }
        }
    };
    scenes.push_back(scene("form-fill", "forms/forms-all.pdf", fill));
    Scene dark = scene("form-fill", "forms/forms-all.pdf", fill);
    dark.name = QStringLiteral("form-fill-dark");
    dark.theme = ThemeChoice::Dark;
    scenes.push_back(std::move(dark));
    scenes.push_back(scene("form-highlight", "forms/forms-all.pdf", [](MainWindow&, DocumentView& doc) {
        doc.forms().setHighlight(true);
        if (const forms::Field* agree = doc.forms().form().find("agree")) {
            const forms::Widget& w = agree->widgets.front();
            doc.forms().activate(w.page, w.objnum);  // флажок снят — и в фокусе
        }
    }));
    scenes.push_back(scene("form-combo-open", "forms/forms-all.pdf", [](MainWindow& w, DocumentView& doc) {
        doc.forms().setHighlight(false);
        const forms::Field* city = doc.forms().form().find("city");
        if (city == nullptr ||
            !doc.forms().activate(city->widgets.front().page, city->widgets.front().objnum)) {
            return;
        }
        FieldEditor* editor = doc.forms().editor();
        auto* combo = editor != nullptr ? qobject_cast<QComboBox*>(editor->input()) : nullptr;
        if (combo == nullptr) {
            return;
        }
        // Список — окно верхнего уровня: снимок окна его не видит — встроить под поле (как меню menu-open).
        doc.view().ensureVisible(city->widgets.front().page, city->widgets.front().rect, 200);
        editor->place();
        combo->showPopup();
        QWidget* popup = combo->view()->window();
        const QPoint at = combo->mapTo(&w, QPoint(0, combo->height()));
        popup->setParent(&w, Qt::Widget);
        const int rows = combo->count();
        const int row = std::max(combo->view()->sizeHintForRow(0), combo->height());
        popup->setGeometry(QRect(at, QSize(combo->width(), rows * row + 4)));
        popup->show();
        popup->raise();
    }));
    scenes.push_back(scene("form-js-bar", "forms/forms-all.pdf", [](MainWindow&, DocumentView& doc) {
        doc.forms().setHighlight(false);
        doc.view().setZoomPercent(75);
    }));
    scenes.push_back(scene("form-js-alert", "forms/forms-all.pdf", [](MainWindow& w, DocumentView& doc) {
        doc.forms().setHighlight(false);
        forms::AlertRequest r;
        r.message = QCoreApplication::translate("ot::ui::FormScenes", "Количество: только цифры, а не abc")
                        .toStdString();
        r.icon = forms::AlertIcon::Warning;
        r.buttons = forms::AlertButtons::Ok;
        auto* dialog = new JsAlertDialog(r, &w);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        embedCentered(w, dialog);
    }));
    scenes.push_back(scene("form-xfa-bar", "forms/xfa-stub.pdf", [](MainWindow&, DocumentView&) {}));
    return scenes;
}

}  // namespace ot::ui
