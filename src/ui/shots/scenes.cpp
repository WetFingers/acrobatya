#include "ui/shots/scenes.h"

#include <QApplication>
#include <QDir>
#include <QPixmap>
#include <QScrollArea>

#include <cstdio>
#include <memory>

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
        auto* w = new MainWindow();
        setup(*w);
        return w;
    };
}

// Таблица сцен (эталон 6.10 — SCENES): порядок — порядок «all» и контакт-листов.
const std::vector<Scene>& sceneTable() {
    static const std::vector<Scene> SCENES = {
        {QStringLiteral("home"), withMainWindow([](MainWindow& w) { w.showHome(); }), {}, {}},
        {QStringLiteral("showcase"), showcaseScene, {}, {}},
        {QStringLiteral("showcase-dark"), showcaseScene, ThemeChoice::Dark, {}},
        {QStringLiteral("showcase-contrast"), showcaseScene, ThemeChoice::HighContrast, {}},
        {QStringLiteral("showcase-large"), showcaseScene, {}, 1.25},
    };
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

int runShots(const QStringList& names, const ShotOptions& options) {
    const QString out = options.outDir.isEmpty() ? QStringLiteral(".queue/screenshots") : options.outDir;
    QDir().mkpath(out);
    QStringList wanted = names;
    if (wanted.contains(QStringLiteral("all"))) {
        wanted = sceneNames();
    }
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
