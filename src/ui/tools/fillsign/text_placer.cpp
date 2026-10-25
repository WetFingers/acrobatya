// Текст Fill & Sign (план 5.3, п. 2; эталон 3.5: Текст — FreeText без рамки, кегль авто под высоту строки;
// умное размещение; forms-07, comment-09): щелчок — core/annot/fillsign.h placeText по геометрии бланка
// страницы: на подчёркивании — с точки щелчка базовой линией на линии, в клетке — от её левого края по центру
// высоты, вне полей — с точки щелчка кеглем панели; набор — InlineTextEditor (поле создаётся при фиксации
// непустого текста, шаг «Add text box»); гребёнка — CombEditor (по символу в клетку, шаг «Add text»).
// Щелчок по своему полю — правка на странице (fillsign_mode.cpp передаёт его сюда).
#include <QFuture>
#include <QPointer>

#include "services/annot_ops.h"
#include "services/comment_settings.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "ui/tools/comment/inline_text_editor.h"
#include "ui/tools/fillsign/comb_editor.h"
#include "ui/tools/fillsign/fillsign_mode.h"
#include "ui/view/page_view.h"

namespace ot::ui {

void FillSignMode::placeText(int page, QPointF point, const fillsign::PageGeometry& geometry) {
    const fillsign::Placement placement = fillsign::placeText(
        geometry, PointF{static_cast<float>(point.x()), static_cast<float>(point.y())}, fontSize_);
    if (placement.kind == fillsign::FieldKind::Comb) {
        startComb(page, placement);
        return;
    }
    const auto pageWidth = static_cast<float>(view_.pageSizePoints(page).width());
    annot::AnnotProps props = fillsign::textProps(placement, {}, annotColor(), pageWidth);
    props.author = comments::author(Settings());
    InlineTextEditor* editor = InlineTextEditor::startNew(view_, page, std::move(props));
    connect(editor, &InlineTextEditor::committed, this, [this](const annot::AnnotRef& ref) {
        if (ref.valid()) {
            emit created(ref);
        }
    });
}

void FillSignMode::startComb(int page, const fillsign::Placement& placement) {
    CombEditor* editor = CombEditor::start(view_, page, placement, ink_);
    connect(editor, &CombEditor::committed, this, [this](const std::vector<annot::AnnotRef>& refs) {
        if (!refs.empty()) {
            emit created(refs.front());
        }
    });
}

void FillSignMode::editText(const annot::AnnotRef& ref) {
    DocumentSession* s = session();
    if (s == nullptr) {
        return;
    }
    const QPointer<PageView> view(&view_);
    annot::props(*s, ref).then(this, [view, ref](QFuture<std::optional<annot::AnnotProps>> f) {
        std::optional<annot::AnnotProps> p;
        try {
            p = f.result();
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): поля нет — править нечего
        }
        if (view != nullptr && p && p->type == annot::AnnotType::FreeText) {
            InlineTextEditor::startEdit(*view, ref, *p);
        }
    });
}

}  // namespace ot::ui
