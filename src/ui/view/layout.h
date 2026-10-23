// Раскладка страниц вида (эталон 3.1 «Раскладка», 4.2; view-03, view-04): одна функция layoutPages() для
// всех режимов — одна страница, непрерывно, разворот, разворот непрерывно и «титул отдельно» (флаг разворота:
// первая страница одна, справа — как /PageLayout TwoColumnRight/TwoPageRight). Раскладка знает только
// размеры страниц (пункты, с /CropBox и /Rotate — OpenSummary::pageSizes; не кратный 90 /Rotate MuPDF уже
// округлил) — 1 200 страниц раскладываются за доли миллисекунды, рендер ленивый.
//
// Координаты — логические пиксели документа: страница — size × zoom, при повороте вида на 90/270 ширина и
// высота меняются местами; зазор gap (8 px) между страницами, рядами и до краёв. Один столбец — страницы по
// центру общей ширины (самая широкая страница + 2 зазора). Разворот — «корешок»: левая страница прижата к
// нему справа, правая — слева; ширина столбцов — самые широкие левая и правая страницы (страницы не прыгают
// при листании). В ряду страницы центрированы по высоте. Непрерывные режимы — ряды один под другим;
// постраничные (одна страница, разворот) — каждый ряд сам по себе от y = gap: виден один ряд, высота
// содержимого — его высота.
#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <optional>
#include <vector>

namespace ot::ui {

enum class PageMode { SinglePage, Continuous, Facing, FacingContinuous };

struct LayoutOptions {
    PageMode mode = PageMode::Continuous;
    bool coverPage = false;  // титул отдельно (только разворот)
    float zoom = 1.0f;       // логических px на пункт
    int rotation = 0;        // поворот вида: 0, 90, 180, 270 (документ не меняется)
    float gap = 8.0f;        // логические px

    bool operator==(const LayoutOptions&) const = default;
};

bool isPaged(PageMode mode) noexcept;
bool isFacing(PageMode mode) noexcept;

// /PageLayout каталога → режим и «титул отдельно»: SinglePage, OneColumn, TwoColumnLeft/Right,
// TwoPageLeft/Right; незнакомое и пустое — nullopt (режим по настройке).
struct PdfPageLayout {
    PageMode mode = PageMode::Continuous;
    bool coverPage = false;
};
std::optional<PdfPageLayout> pageModeFromPdf(const QString& name);

struct PageSlot {
    QRectF rect;  // логические px: в непрерывных режимах — документа, в постраничных — своего ряда
    int row = 0;
    int column = 0;  // 0 — единственная или левая, 1 — правая страница разворота
};

struct LayoutRow {
    int first = 0;  // первая страница ряда
    int count = 1;  // 1 или 2
    QRectF rect;    // охват страниц ряда (без зазоров)
};

class PageLayout {
public:
    const LayoutOptions& options() const noexcept { return options_; }
    bool paged() const noexcept { return isPaged(options_.mode); }
    int pageCount() const noexcept { return static_cast<int>(slots_.size()); }
    int rowCount() const noexcept { return static_cast<int>(rows_.size()); }
    const PageSlot& slot(int page) const { return slots_.at(static_cast<std::size_t>(page)); }
    const std::vector<PageSlot>& pageSlots() const noexcept { return slots_; }
    const std::vector<LayoutRow>& rows() const noexcept { return rows_; }
    int rowOf(int page) const;
    // Размер страницы на экране (логические px, с поворотом вида).
    QSizeF displaySize(int page) const { return slot(page).rect.size(); }

    // Размер содержимого: непрерывные режимы — весь документ; постраничные — ряд row (ширина — общая).
    QSizeF contentSize(int row = 0) const;
    // Страница под точкой содержимого (для постраничных — в ряду row); −1 — зазор или вне страниц.
    int pageAt(QPointF point, int row = 0) const;
    // Ближайшая к точке страница (зазор → соседняя); пусто — −1.
    int nearestPage(QPointF point, int row = 0) const;
    // Страницы, пересекающие прямоугольник содержимого, по возрастанию (постраничные — только ряд row).
    std::vector<int> pagesIn(const QRectF& area, int row = 0) const;

private:
    friend PageLayout layoutPages(const QVector<QSizeF>& pageSizes, const LayoutOptions& options);

    LayoutOptions options_;
    std::vector<PageSlot> slots_;
    std::vector<LayoutRow> rows_;
    float width_ = 0;
    float height_ = 0;  // непрерывные: весь документ
};

PageLayout layoutPages(const QVector<QSizeF>& pageSizes, const LayoutOptions& options);

}  // namespace ot::ui
