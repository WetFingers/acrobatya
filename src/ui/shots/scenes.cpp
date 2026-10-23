#include "ui/shots/scenes.h"

#include <QApplication>
#include <QDir>
#include <QPixmap>
#include <QTextStream>

#include <cstdio>
#include <functional>

#include "ui/shell/main_window.h"
#include "ui/theme/theme.h"

namespace ot::ui {

namespace {

struct Scene {
    const char* name;
    std::function<QWidget*()> make;
};

const std::vector<Scene>& scenes() {
    static const std::vector<Scene> list = {
        {"home",
         [] {
             auto* w = new MainWindow();
             w->showHome();
             return static_cast<QWidget*>(w);
         }},
    };
    return list;
}

void printLine(const QString& s) {
    const QByteArray utf8 = s.toUtf8();
    std::fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

}  // namespace

QStringList sceneNames() {
    QStringList names;
    for (const Scene& s : scenes()) {
        names << QString::fromLatin1(s.name);
    }
    return names;
}

int runShots(const QStringList& names, const QString& outDir, QSize size, const QString& theme) {
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    Theme::apply(*app, parseThemeChoice(theme).value_or(ThemeChoice::Light));
    const QString out = outDir.isEmpty() ? QStringLiteral(".queue/screenshots") : outDir;
    QDir().mkpath(out);
    QStringList wanted = names;
    if (wanted.contains(QStringLiteral("all"))) {
        wanted = sceneNames();
    }
    int failures = 0;
    for (const QString& name : wanted) {
        const Scene* scene = nullptr;
        for (const Scene& s : scenes()) {
            if (name == QLatin1String(s.name)) {
                scene = &s;
            }
        }
        if (scene == nullptr) {
            printLine(QStringLiteral("unknown scene: ") + name);
            ++failures;
            continue;
        }
        std::unique_ptr<QWidget> w(scene->make());
        w->resize(size);
        w->show();
        QApplication::processEvents();
        const QString suffix = theme == QLatin1String("dark") ? QStringLiteral("-dark") : QString();
        const QString file = QDir(out).filePath(name + suffix + QStringLiteral(".png"));
        const QPixmap pm = w->grab();
        if (pm.isNull() || !pm.save(file, "PNG")) {
            printLine(QStringLiteral("FAILED ") + file);
            ++failures;
        } else {
            printLine(QDir::toNativeSeparators(file) +
                      QStringLiteral(" %1x%2").arg(pm.width()).arg(pm.height()));
        }
        w->close();
    }
    return failures == 0 ? 0 : 1;
}

}  // namespace ot::ui
