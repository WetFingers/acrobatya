#include "ui/shots/scenes.h"
#include "ui/tools/tool_registry.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QPixmap>
#include <QScrollArea>
#include <QThread>

#include <cstdio>
#include <memory>

#include "services/i18n.h"
#include "services/print_job.h"
#include "services/settings.h"
#include "services/shell_launcher.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/widgets/showcase.h"

namespace ot::ui {

namespace {

void printLine(const QString& s) {
    const QByteArray utf8 = s.toUtf8();
    std::fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// Витрина в прокрутке без рамки: крупные элементы (× 1,25) не помещаются в 1440 × 900 — снимок показывает
// верх.
QWidget* showcaseScene() {
    auto* scroll = new QScrollArea();
    scroll->setObjectName(QStringLiteral("showcaseScene"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(createShowcase());
    return scroll;
}

QApplication& application() { return *qobject_cast<QApplication*>(QCoreApplication::instance()); }

ThemeChoice sceneTheme(const Scene& s, const ShotOptions& o) { return s.theme.value_or(o.theme); }
double sceneScale(const Scene& s, const ShotOptions& o) { return s.scale.value_or(o.scale); }

}  // namespace

std::function<QWidget*()> withMainWindow(std::function<void(MainWindow&)> setup) {
    return [setup = std::move(setup)]() -> QWidget* {
        // Состояние панелей прошлой сцены (F8, Shift+F4, правая навигация) — не в следующую: данные снимков
        // общие, сцены — независимые.
        {
            Settings ini;
            ini.reset(settings::kPanelsHidden.name);
            ini.reset(settings::kToolPanelCollapsed.name);
            ini.reset(settings::kNavPaneGroup);
        }
        ToolRegistry::instance().deactivate();  // инструмент прошлой сцены (реестр — один на процесс)
        auto* w = new MainWindow();
        // Сцена не спрашивает «Сохранить изменения?» при закрытии (модальный вопрос без экрана не закрыть).
        w->setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        setup(*w);
        return w;
    };
}

// Таблица сцен (эталон 6.10 — SCENES): порядок — порядок «all» и контакт-листов.
const std::vector<Scene>& sceneTable() {
    static const std::vector<Scene> SCENES = [] {
        std::vector<Scene> table = {
            homeScene("home"),
            {QStringLiteral("showcase"), showcaseScene, {}, {}, {}},
            {QStringLiteral("showcase-dark"), showcaseScene, ThemeChoice::Dark, {}, {}},
            {QStringLiteral("showcase-contrast"), showcaseScene, ThemeChoice::HighContrast, {}, {}},
            {QStringLiteral("showcase-large"), showcaseScene, {}, 1.25, {}},
        };
        for (Scene& s : viewScenes()) {  // 2.2b: view-single … view-loupe
            table.push_back(std::move(s));
        }
        for (Scene& s : shellScenes()) {  // 2.3: shell-document, shell-two-tabs, all-tools…, quick-bar
            table.push_back(std::move(s));
        }
        for (Scene& s : homeScenes()) {  // 2.3: home-empty, home-recent, home-files
            table.push_back(std::move(s));
        }
        for (Scene& s : paneScenes()) {  // 2.4: pane-thumbnails, pane-bookmarks, pane-layers…
            table.push_back(std::move(s));
        }
        for (Scene& s : statusScenes()) {  // 2.4: status-bar, message-bar-stack
            table.push_back(std::move(s));
        }
        for (Scene& s : modeScenes()) {  // 2.4: read-mode, full-screen
            table.push_back(std::move(s));
        }
        for (Scene& s : prefsScenes()) {  // 2.4b: preferences-general, preferences-page-display, palette
            table.push_back(std::move(s));
        }
        for (Scene& s : searchScenes()) {  // 2.5: find-bar, search-window
            table.push_back(std::move(s));
        }
        for (Scene& s : dialogScenes()) {  // 2.5: properties-*, xmp-editor
            table.push_back(std::move(s));
        }
        for (Scene& s : trustScenes()) {  // 2.5b: protected-view, link-confirm
            table.push_back(std::move(s));
        }
        for (Scene& s : renderScenes()) {  // 2.5b: view-night, view-high-contrast, view-overprint
            table.push_back(std::move(s));
        }
        for (Scene& s : printScenes()) {  // 2.6: print-dialog, print-dialog-booklet, print-dialog-poster…
            table.push_back(std::move(s));
        }
        for (Scene& s : editScenes()) {  // 3.4: edit-mode, edit-caret, edit-readonly-block…
            table.push_back(std::move(s));
        }
        for (Scene& s : signScenes()) {  // 4.6: sign-dialog, certify-dialog, панель «Подписи», полосы…
            table.push_back(std::move(s));
        }
        return table;
    }();
    return SCENES;
}

QStringList sceneNames() {
    QStringList names;
    for (const Scene& s : sceneTable()) {
        names << s.name;
    }
    return names;
}

const Scene* findScene(const QString& name) {
    for (const Scene& s : sceneTable()) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

QString sceneFileName(const Scene& scene, const ShotOptions& options) {
    QString name = scene.name;
    if (!scene.theme && options.theme != ThemeChoice::Light) {
        name += QLatin1Char('-') + themeChoiceName(options.theme);
    }
    if (!scene.scale && options.scale > 1.0) {
        name += QStringLiteral("-large");
    }
    if (options.lang != QLatin1String("ru")) {
        name += QLatin1Char('-') + options.lang;
    }
    return name + QStringLiteral(".png");
}

QWidget* makeScene(const QString& name, const ShotOptions& options) {
    const Scene* scene = findScene(name);
    if (scene == nullptr) {
        return nullptr;
    }
    Theme::apply(application(), sceneTheme(*scene, options), sceneScale(*scene, options));
    QWidget* w = scene->make();
    w->resize(options.size);
    return w;
}

bool waitSceneReady(const Scene& scene, QWidget* window, int timeoutMs) {
    if (!scene.ready) {
        return true;
    }
    QElapsedTimer t;
    t.start();
    while (!scene.ready(window)) {
        if (t.elapsed() > timeoutMs) {
            return false;
        }
        QApplication::processEvents();
        QThread::msleep(5);
    }
    return true;
}

int runShots(const QStringList& names, const ShotOptions& options) {
    const QString out = options.outDir.isEmpty() ? QStringLiteral(".queue/screenshots") : options.outDir;
    QDir().mkpath(out);
    QStringList wanted = names;
    if (wanted.contains(QStringLiteral("all"))) {
        wanted = sceneNames();
    }
    // Снимки — на языке варианта, а не системы и не настройки (сцены детерминированы).
    ot::i18n::install(options.lang);
    // Оболочка Windows в сценах — только фейк (эталон 8.3): сцена ничего не открывает на машине человека.
    static FakeShellLauncher shell;
    ShellLauncher::setInstance(&shell);
    static ScopedPrintDevices printers(demoPrinters(), demoPrinterFactory());  // принтеры человека не видны
    const bool variant = options.theme != ThemeChoice::Light || options.scale > 1.0;
    int failures = 0;
    for (const QString& name : wanted) {
        const Scene* scene = findScene(name);
        if (scene == nullptr) {
            printLine(QStringLiteral("unknown scene: ") + name);
            ++failures;
            continue;
        }
        if (variant && (scene->theme || scene->scale)) {
            printLine(QStringLiteral("skip ") + name + QStringLiteral(" (theme/scale fixed by the scene)"));
            continue;
        }
        std::unique_ptr<QWidget> w(makeScene(name, options));
        w->show();
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents();
        QApplication::processEvents();
        if (!waitSceneReady(*scene, w.get())) {
            printLine(QStringLiteral("not ready in 30 s: ") + name);
            ++failures;
        }
        const QString file = QDir(out).filePath(sceneFileName(*scene, options));
        const QPixmap pm = w->grab();
        if (pm.isNull() || !pm.save(file, "PNG")) {
            printLine(QStringLiteral("FAILED ") + file);
            ++failures;
        } else {
            printLine(QDir::toNativeSeparators(file) +
                      QStringLiteral(" %1x%2").arg(pm.width()).arg(pm.height()));
        }
        w->close();
        w.release()->deleteLater();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    return failures == 0 ? 0 : 1;
}

}  // namespace ot::ui
