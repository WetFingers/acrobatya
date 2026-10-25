// Картинки подписи и инициалов Fill & Sign (план 5.3, п. 1; эталон 3.5: «Набрать (Segoe Script / Segoe
// Print), Нарисовать (сглаженный Ink → PNG с альфой), Картинка (PNG/JPEG, удаление белого фона по порогу)»;
// sign-01): всё — ARGB32 без предумножения (цвет пикселя читается как есть), фон прозрачный, картинка
// обрезана по чернилам. Набранная подпись рисуется системным рукописным шрифтом (вопрос 12.9 — шрифтов в
// репозитории нет); ей нужен QGuiApplication (шрифты), остальному — нет. Только Qt Core/Gui (без Widgets).
#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <vector>

namespace ot::fillsign {

// Порог яркости фона (0–255): светлее — бумага, прозрачно.
inline constexpr int kDefaultThreshold = 200;
// Поле вокруг чернил после обрезки, px.
inline constexpr int kInkMargin = 4;
// Рукописные шрифты Windows для «Набрать» (вопрос 12.9): первый — по умолчанию.
inline constexpr const char* kTypedFonts[] = {"Segoe Script", "Segoe Print"};

// Убрать белый фон: пиксель ярче threshold — прозрачный; темнее threshold/2 — непрозрачный; между — мягкий
// край. keepColor — цвет пикселя как в источнике («Сохранить исходный цвет»), иначе — ink.
QImage removeBackground(const QImage& source, int threshold, bool keepColor, const QColor& ink);
// Обрезать по пикселям с альфой > 0 и поле margin px; чернил нет — пустая картинка.
QImage cropToInk(const QImage& image, int margin = kInkMargin);
// Перекрасить чернила в color (альфа прежняя).
QImage recolor(const QImage& image, const QColor& color);
// Штрихи мышью (точки в px холста canvas) → картинка в scale раз крупнее: сглаженные кривые (квадратичные
// Безье через середины отрезков), круглые концы, толщина width px холста; обрезана по чернилам.
QImage strokesToImage(const std::vector<std::vector<QPointF>>& strokes, const QSizeF& canvas,
                      const QColor& color, double width, double scale = 2.0);
// Набранная подпись: text шрифтом family высотой pixelSize px; обрезана по чернилам; пустой текст — пусто.
QImage typedImage(const QString& text, const QString& family, const QColor& color, int pixelSize = 96);
// Инициалы по имени: первые буквы слов (до трёх) заглавными с точками — «Иван Петров» → «И.П.».
QString initialsOf(const QString& name);

// Есть хоть один пиксель с альфой ≥ minAlpha.
bool hasInk(const QImage& image, int minAlpha = 32);
// Связные области чернил (альфа ≥ minAlpha, 8-связность) — проверка нарисованного.
int inkComponents(const QImage& image, int minAlpha = 32);
// PNG с альфой; пустая — пусто.
QByteArray toPng(const QImage& image);

}  // namespace ot::fillsign
