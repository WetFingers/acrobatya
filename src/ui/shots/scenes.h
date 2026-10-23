// Сцены для снимков без экрана (эталон 6.10): AcroBatya.exe --shots <имя[,имя]|all> --out <каталог>
// [--theme light|dark|contrast] [--scale 1.25] [--lang ru|en]; --shots-list печатает имена.
// Новая сцена — строка в таблице SCENES (sceneTable() в scenes.cpp): имя kebab-case и функция, которая строит
// окно; сцена может закрепить тему или масштаб (showcase-dark, showcase-large) — тогда вариант запуска её не
// меняет. Закон вида — эталон 6, не снимок; pHash к прошлому рубежу (tools/review_refs.py --shots) — сигнал.
#pragma once

#include <QSize>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

#include "ui/theme/theme.h"

class QWidget;

namespace ot::ui {

class MainWindow;

struct Scene {
    QString name;
    std::function<QWidget*()> make;    // окно сцены (вызывающий владеет)
    std::optional<ThemeChoice> theme;  // закреплённая тема сцены
    std::optional<double> scale;       // закреплённый масштаб («Крупные элементы»)
};

struct ShotOptions {
    QString outDir;  // пусто — .queue/screenshots
    QSize size{1440, 900};
    ThemeChoice theme = ThemeChoice::Light;
    double scale = 1.0;
    QString lang = QStringLiteral("ru");
};

const std::vector<Scene>& sceneTable();
QStringList sceneNames();
const Scene* findScene(const QString& name);

// Окно сцены с темой и масштабом сцены или варианта (без снимка) — для тестов.
QWidget* makeScene(const QString& name, const ShotOptions& options = {});
// Имя PNG: <сцена>[-dark|-contrast][-large][-en].png; закреплённое сценой в суффикс не идёт.
QString sceneFileName(const Scene& scene, const ShotOptions& options);
// Снять сцены (names может содержать "all"); код для main: 0 — все сняты. Сцены с закреплённой темой или
// масштабом в варианте с другой темой или масштабом пропускаются — они уже сняты в основном прогоне.
int runShots(const QStringList& names, const ShotOptions& options);

// Сцена из главного окна: MainWindow, доведённый функцией до нужного вида.
std::function<QWidget*()> withMainWindow(std::function<void(MainWindow&)> setup);

// fixtures.cpp — данные сцен без сети и данных человека: tests/fixtures/scenes и tests/fixtures/demo.
QString sourceRoot();                           // OTTISK_SOURCE_DIR или вверх от exe до tests/fixtures
QString sceneFixture(const QString& relative);  // "demo/contract.pdf" → абсолютный путь (пусто — нет файла)

}  // namespace ot::ui
