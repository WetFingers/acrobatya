// Сцены строки состояния и полос сообщений (план 2.4; эталон 6.1, 6.6, 6.10): status-bar — отчёт с
// логическими номерами (стр. «2» — четвёртая из 12), идёт фоновая задача с прогрессом; message-bar-stack —
// стек полос по важности: защищённый просмотр, подписи, PDF/A, форма (тексты — как их покажут
// сессии-владельцы 2.5, 2.5b, 4.6, 6.1). Кадр снимается, когда видимые тайлы готовы.
#include <QPushButton>

#include <vector>

#include "services/background_tasks.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/message_bar.h"
#include "ui/shell/status_bar.h"
#include "ui/shots/scenes.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"
#include "ui/widgets/message_bar.h"

namespace ot::ui {

namespace {

bool statusDocumentReady(QWidget* w) {
    auto* window = static_cast<MainWindow*>(w);
    DocumentView* doc = window->currentDocument();
    if (doc == nullptr || doc->view().pageCount() == 0) {
        return false;
    }
    doc->view().viewport()->repaint();
    return doc->view().tilePainter().visibleComplete();
}

}  // namespace

std::vector<Scene> statusScenes() {
    std::vector<Scene> scenes;

    Scene status;
    status.name = QStringLiteral("status-bar");
    status.make = withMainWindow([](MainWindow& w) {
        w.openFile(sceneFixture(QStringLiteral("demo/report.pdf")), 3);
        const quint64 task =
            BackgroundTasks::shared().start(MainWindow::tr("Сохранение «report.pdf» как PDF"), [] {});
        BackgroundTasks::shared().setProgress(task, 40);
        QObject::connect(&w, &QObject::destroyed, [task] { BackgroundTasks::shared().finish(task); });
    });
    status.ready = statusDocumentReady;
    scenes.push_back(status);

    Scene stack;
    stack.name = QStringLiteral("message-bar-stack");
    stack.make = withMainWindow([](MainWindow& w) {
        w.openFile(sceneFixture(QStringLiteral("demo/form.pdf")));
        MessageStack& s = w.currentDocument()->messageStack();
        // Показ не по порядку — стек ставит полосы по важности.
        s.showMessage(MessageKind::Form, MainWindow::tr("Документ содержит заполняемые поля."))
            ->addAction(MainWindow::tr("Подсветить поля"), QStringLiteral("highlight"));
        s.showMessage(MessageKind::PdfA,
                      MainWindow::tr("Этот файл заявляет соответствие PDF/A и открыт только для чтения."))
            ->addAction(MainWindow::tr("Включить редактирование"), QStringLiteral("edit"));
        s.showMessage(MessageKind::SignatureValid, MainWindow::tr("Подписано, все подписи действительны."))
            ->addAction(MainWindow::tr("Панель подписей"), QStringLiteral("signatures"));
        s.showMessage(MessageKind::ProtectedView,
                      MainWindow::tr("Защищённый просмотр: файл получен из Интернета, правка отключена."))
            ->addAction(MainWindow::tr("Включить все функции"), QStringLiteral("trust"), true);
    });
    stack.ready = statusDocumentReady;
    scenes.push_back(stack);
    return scenes;
}

}  // namespace ot::ui
