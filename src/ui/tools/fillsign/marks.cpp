// Отметки Fill & Sign (план 5.3, п. 2; эталон 3.5: ✓, ✕, ●, —, ▭ — FreeText с символом из Segoe UI Symbol как
// глиф, не картинка): щелчок во флажке бланка — отметка по его центру и размеру, иначе — центром в точке
// щелчка кеглем панели × 1,4; шаг «Add mark»; поставленная выбрана — её можно сразу сдвинуть, растянуть или
// удалить.
#include "services/annot_ops.h"
#include "services/comment_settings.h"
#include "services/document_session.h"
#include "services/fillsign_ops.h"
#include "services/settings.h"
#include "ui/tools/fillsign/fillsign_mode.h"

namespace ot::ui {

void FillSignMode::placeMark(int page, QPointF point, const fillsign::PageGeometry& geometry) {
    DocumentSession* s = session();
    if (s == nullptr) {
        return;
    }
    const fillsign::Placement placement = fillsign::placeMark(
        geometry, PointF{static_cast<float>(point.x()), static_cast<float>(point.y())}, fontSize_ * 1.4f);
    annot::AnnotProps props = fillsign::markProps(markOf(action_), placement, annotColor());
    props.author = comments::author(Settings());
    finishCreate(annot::create(*s, page, std::move(props), QString::fromLatin1(fillsign::kAddMarkStep)),
                 true);
}

}  // namespace ot::ui
