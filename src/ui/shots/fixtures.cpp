// Фикстуры сцен (эталон 6.10): договор с полями и подписью, скан, многостраничный с закладками — в
// tests/fixtures/scenes и tests/fixtures/demo исходников; программа их не поставляет, сцены ищут дерево
// исходников.
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include "ui/shots/scenes.h"

namespace ot::ui {

QString sourceRoot() {
    const QString env = qEnvironmentVariable("OTTISK_SOURCE_DIR");
    if (!env.isEmpty() && QDir(env).exists(QStringLiteral("tests/fixtures"))) {
        return QDir(env).absolutePath();
    }
    // build/<пресет>/bin/AcroBatya.exe → корень репозитория на три уровня выше.
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 5; ++i) {
        if (dir.exists(QStringLiteral("tests/fixtures"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

QString sceneFixture(const QString& relative) {
    const QString root = sourceRoot();
    if (root.isEmpty()) {
        return {};
    }
    for (const QString& base :
         {QStringLiteral("tests/fixtures/"), QStringLiteral("tests/fixtures/scenes/")}) {
        const QFileInfo fi(QDir(root).filePath(base + relative));
        if (fi.isFile()) {
            return fi.absoluteFilePath();
        }
    }
    return {};
}

}  // namespace ot::ui
