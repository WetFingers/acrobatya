// Набор в гребёнку Fill & Sign (план 5.3, п. 2; эталон 3.5: «гребёнка (одна буква в клетку) — режим с
// шагом»): строка поверх клеток моноширинным Courier New с межбуквенным интервалом в шаг клетки, не длиннее
// числа клеток. Фиксация — Enter, Esc, потеря фокуса, смена документа: по символу в клетку — поля FreeText
// одним шагом «Add text» (core/annot/fillsign.h combProps, services/fillsign_ops.h createAll).
#pragma once

#include <QColor>
#include <QLineEdit>

#include <vector>

#include "core/annot/annots.h"
#include "core/annot/fillsign.h"

namespace ot::ui {

class PageView;

class CombEditor : public QLineEdit {
    Q_OBJECT
public:
    static CombEditor* start(PageView& view, int page, fillsign::Placement placement, const QColor& color);
    static CombEditor* active(const PageView& view);

    int page() const noexcept { return page_; }
    const fillsign::Placement& placement() const noexcept { return placement_; }
    // Записать набранное (один раз) и закрыть.
    void commit();

signals:
    void committed(const std::vector<ot::annot::AnnotRef>& refs);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    CombEditor(PageView& view, int page, fillsign::Placement placement, const QColor& color);
    void applyFormat();

    PageView& view_;
    int page_ = -1;
    fillsign::Placement placement_;
    QColor color_;
    bool done_ = false;
};

}  // namespace ot::ui
