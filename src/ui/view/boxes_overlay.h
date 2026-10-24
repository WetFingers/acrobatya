// Слой рамок областей (план 2.5b; эталон 3.1 «Сглаживание и качество… показать Art/Trim/Bleed»; view-36):
// при настройке pageDisplay/showPageBoxes вид обводит заданные в документе рамки — выпуск (/BleedBox),
// обрезку (/TrimBox) и иллюстрацию (/ArtBox) — цветами kPageBoxColors (данные документа, не тема) тонкой
// линией поверх страницы. Рамки приходят задачей документа (services/box_ops.h) при первом рисовании;
// правка страницы и смена документа их сбрасывают. Координаты — пункты страницы MuPDF (pageTransform).
#pragma once

#include <QHash>
#include <QSet>

#include <optional>

#include "core/pages/boxes.h"
#include "ui/view/overlay.h"

namespace ot::ui {

class PageView;

class BoxesOverlay : public Overlay {
public:
    explicit BoxesOverlay(PageView& view) : view_(view) {}

    void setShown(bool on);
    bool isShown() const noexcept { return shown_; }

    // Рамки загруженной страницы; nullopt — ещё не пришли (запрос — при рисовании).
    std::optional<pages::PageBoxes> boxes(int page) const;
    bool hasBoxes(int page) const { return boxes_.contains(page); }

    void documentChanged();
    void pageChanged(int page);

    void paintPage(QPainter& painter, const PageView& view, int page) override;

private:
    void request(int page);

    PageView& view_;
    bool shown_ = false;
    QHash<int, pages::PageBoxes> boxes_;
    QSet<int> requested_;
    quint64 epoch_ = 0;
};

}  // namespace ot::ui
