// Слои поверх страниц PageView (план 2.2; эталон 3.1, 3.2): выделение текста, подсветка поиска, рамки
// аннотаций, курсор и рамка инструмента. Слой рисует в координатах вьюпорта; страница → вьюпорт —
// PageView::pageTransform(page) (пункты MuPDF, поворот вида учтён). OverlayStack хранит слои по порядку z
// (меньше — ниже) и не владеет ими: встроенные слои — члены PageView, слой инструмента — у ToolController.
#pragma once

#include <QColor>
#include <QHash>
#include <QPainter>

#include <functional>
#include <utility>
#include <vector>

#include "core/util/geometry.h"

namespace ot::ui {

class PageView;

class Overlay {
public:
    virtual ~Overlay() = default;
    // Поверх страницы page (painter обрезан её рамкой на экране).
    virtual void paintPage(QPainter& /*painter*/, const PageView& /*view*/, int /*page*/) {}
    // Поверх всего вьюпорта (рамка инструмента, курсор) — после страниц.
    virtual void paintViewport(QPainter& /*painter*/, const PageView& /*view*/) {}
};

class OverlayStack {
public:
    void add(Overlay* overlay, int z = 0);
    void remove(Overlay* overlay);
    bool contains(const Overlay* overlay) const;
    int size() const noexcept { return static_cast<int>(layers_.size()); }

    void paintPage(QPainter& painter, const PageView& view, int page) const;
    void paintViewport(QPainter& painter, const PageView& view) const;

private:
    std::vector<std::pair<int, Overlay*>> layers_;  // по z, равные — по порядку добавления
};

// Залитые четырёхугольники по страницам (пункты страницы): выделение текста, совпадения поиска. Текущее
// совпадение — обводкой цветом stroke.
class QuadOverlay : public Overlay {
public:
    using ColorFn = std::function<QColor()>;  // цвет — из темы в момент рисования
    explicit QuadOverlay(ColorFn fill, ColorFn stroke = {})
        : fill_(std::move(fill)), stroke_(std::move(stroke)) {}

    void setQuads(int page, std::vector<QuadF> quads);
    void clear();
    void clearPage(int page);
    const std::vector<QuadF>& quads(int page) const;
    bool empty() const noexcept { return quads_.isEmpty(); }
    QList<int> pages() const { return quads_.keys(); }
    // Выделенный (текущий) четырёхугольник: обводка; page < 0 — нет.
    void setCurrent(int page, int index);
    // Несколько текущих (совпадение в две строки или через страницу): пары {страница, индекс}.
    void setCurrent(std::vector<std::pair<int, int>> items);
    const std::vector<std::pair<int, int>>& current() const noexcept { return current_; }

    void paintPage(QPainter& painter, const PageView& view, int page) override;

private:
    ColorFn fill_;
    ColorFn stroke_;
    QHash<int, std::vector<QuadF>> quads_;
    std::vector<std::pair<int, int>> current_;
};

// Рамки по страницам (пункты страницы): выбранные аннотации, области инструмента.
class RectOverlay : public Overlay {
public:
    using ColorFn = std::function<QColor()>;
    explicit RectOverlay(ColorFn stroke) : stroke_(std::move(stroke)) {}

    void setRects(int page, std::vector<RectF> rects);
    void clear();
    const std::vector<RectF>& rects(int page) const;
    bool empty() const noexcept { return rects_.isEmpty(); }

    void paintPage(QPainter& painter, const PageView& view, int page) override;

private:
    ColorFn stroke_;
    QHash<int, std::vector<RectF>> rects_;
};

}  // namespace ot::ui
