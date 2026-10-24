// Рамки областей страницы (план 2.5b — чтение для «Установки → Просмотр страницы → Показывать рамки обрезки,
// подрезки и выпуска», view-36; запись — 7.1 «Обрезать страницы»): /MediaBox, /CropBox, /BleedBox, /TrimBox,
// /ArtBox в координатах страницы MuPDF — как fz_bound_page (fz_bound_page_box: начало — левый верхний угол
// обрезки, y вниз, /Rotate учтён), то есть в тех же пунктах, что слои вида (PageView::pageTransform). has* —
// рамка задана в словаре страницы (с наследованием); незаданная — как у MuPDF (обрезка). Не PDF — только
// обрезка = рамка страницы.
#pragma once

#include "core/util/geometry.h"

namespace ot {

class Document;

namespace pages {

struct PageBoxes {
    RectF media;
    RectF crop;
    RectF bleed;
    RectF trim;
    RectF art;
    bool hasBleed = false;
    bool hasTrim = false;
    bool hasArt = false;
};

// Рамки страницы page (0…). Страница вне диапазона и ошибка MuPDF — MuError.
PageBoxes pageBoxes(Document& doc, int page);

}  // namespace pages
}  // namespace ot
