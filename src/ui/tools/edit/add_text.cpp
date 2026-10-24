// «Добавить текст» и ручки блока режима «Редактировать» (план 3.4, п. 2; эталон 3.9 «Инструмент»; edit-02):
// взведённый режим — щелчок на странице создаёт пустой абзац шириной 200 пт (editing::newParagraph: геометрия
// набора в пространстве пользователя страницы) со стилем из «Установки → Редактирование»; каретка — в нём,
// набор растит блок вниз, запись — новым потоком (пустой блок исчезает без записи). У активного блока — рамка
// с маркерами: за рамку — перемещение (геометрия набора сдвигается, раскладка — тут же сдвигом, точно —
// Rewriter), маркеры слева и справа — ширина набора (перевёрстка задачей).
#include <QPainter>

#include <algorithm>
#include <cmath>

#include "services/document_session.h"
#include "ui/theme/theme.h"
#include "ui/tools/edit/edit_mode.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

namespace {

constexpr float kMinWidth = 18;  // ширина набора не уже, пт

}  // namespace

void EditMode::setAddText(bool armed) {
    if (locked()) {
        armed = false;
    }
    if (armed == addText_) {
        return;
    }
    addText_ = armed;
    view_.viewport()->setCursor(cursor(view_.viewport()->mapFromGlobal(QCursor::pos()), Qt::NoModifier));
    emit addTextChanged(armed);
}

void EditMode::addTextAt(int page, QPointF pagePoint) {
    DocumentSession* session = view_.document();
    if (session == nullptr || locked() || page < 0 || page >= view_.pageCount()) {
        return;
    }
    leaveBlock();
    setAddText(false);
    const quint64 request = ++addRequest_;
    const quint64 epoch = epoch_;
    const edit::CharStyle style = newStyle_;
    const PointF at{static_cast<float>(pagePoint.x()), static_cast<float>(pagePoint.y())};
    editing::newParagraph(*session, page, at, kNewBlockWidth, style)
        .then(this, [this, request, epoch, page, style, at](QFuture<edit::ParagraphEditor> done) {
            if (request != addRequest_ || epoch != epoch_) {
                return;
            }
            edit::ParagraphEditor e;
            try {
                e = done.result();
            } catch (const std::exception&) {
                emit editFailed(QCoreApplication::translate("ot::ui::EditMode", "Не удалось добавить текст"));
                return;
            }
            leaveBlock();
            Active a;
            a.id = ++nextId_;
            a.page = page;
            a.block = -1;
            a.original = RectF{at.x, at.y, at.x, at.y};
            a.editor = e;
            a.clean = e;
            a.base = style;
            a.fresh = true;
            active_ = std::move(a);
            controller_.inputMethodChanged();
            emit editingChanged(true);
            caretMoved();
            view_.setFocus();
        });
}

// ---------------------------------------------------------------- ручки рамки активного блока

EditMode::Handle EditMode::handleAt(QPointF viewportPoint) const {
    const std::optional<QRectF> frame = activeFrame();
    if (!frame || locked()) {
        return Handle::None;
    }
    const Metrics& m = Theme::sizes();
    const QRectF r = frame->adjusted(-2, -2, 2, 2);
    const double h = m.handle;
    const QPointF left(r.left(), r.center().y());
    const QPointF right(r.right(), r.center().y());
    if (QLineF(viewportPoint, left).length() <= h) {
        return Handle::Left;
    }
    if (QLineF(viewportPoint, right).length() <= h) {
        return Handle::Right;
    }
    // Полоса вдоль рамки (снаружи и чуть внутри) — перемещение.
    const QRectF outer = r.adjusted(-h / 2, -h / 2, h / 2, h / 2);
    const QRectF inner = r.adjusted(h / 2, h / 2, -h / 2, -h / 2);
    if (outer.contains(viewportPoint) && !inner.contains(viewportPoint)) {
        return Handle::Move;
    }
    return Handle::None;
}

bool EditMode::pressHandle(QMouseEvent* event) {
    const Handle h = handleAt(event->position());
    if (h == Handle::None) {
        return false;
    }
    drag_ = h;
    dragStart_ = view_.viewportToPage(active_->page, event->position());
    dragLast_ = dragStart_;
    dragWidth0_ = active_->editor.geometry().width;
    view_.setFocus();
    return true;
}

bool EditMode::dragHandle(QPointF viewportPoint) {
    if (!active_ || drag_ == Handle::None) {
        drag_ = Handle::None;
        return false;
    }
    const QPointF now = view_.viewportToPage(active_->page, viewportPoint);
    switch (drag_) {
        case Handle::Move:
            moveActive(now - dragLast_);
            break;
        case Handle::Right:
        case Handle::Left: {
            // Сдвиг по оси строки (раскладка): правый маркер — ширина, левый — край и ширина.
            const Matrix inv = invertMatrix(active_->editor.geometry().toPage);
            auto layoutX = [&](QPointF p) {
                return mapPoint(PointF{static_cast<float>(p.x()), static_cast<float>(p.y())}, inv).x;
            };
            if (drag_ == Handle::Right) {
                setActiveWidth(dragWidth0_ + layoutX(now) - layoutX(dragStart_));
            } else {
                const float step = layoutX(now) - layoutX(dragLast_);
                edit::ParagraphGeometry g = active_->editor.geometry();
                if (g.width - step >= kMinWidth) {
                    g.left += step;
                    g.width -= step;
                    active_->editor.setGeometry(g);
                    changed();
                }
            }
            break;
        }
        case Handle::None:
            break;
    }
    dragLast_ = now;
    return true;
}

void EditMode::moveActive(QPointF pageDelta) {
    if (!active_ || (pageDelta.x() == 0 && pageDelta.y() == 0)) {
        return;
    }
    edit::ParagraphEditor& e = active_->editor;
    edit::ParagraphGeometry g = e.geometry();
    // Сдвиг страницы → сдвиг раскладки (линейная часть обратной матрицы).
    const Matrix inv = invertMatrix(g.toPage);
    const float dx = static_cast<float>(inv.a * pageDelta.x() + inv.c * pageDelta.y());
    const float dy = static_cast<float>(inv.b * pageDelta.x() + inv.d * pageDelta.y());
    g.left += dx;
    g.firstBaseline += dy;
    e.setGeometry(g);
    // Раскладка — тут же сдвигом (точную пришлёт перевёрстка).
    edit::TextLayout layout = e.layout();
    for (float& x : layout.x) {
        x += dx;
    }
    for (edit::LayoutLine& line : layout.lines) {
        line.x0 += dx;
        line.x1 += dx;
        line.baseline += dy;
    }
    if (layout.bounds.valid()) {
        layout.bounds =
            RectF{layout.bounds.x0 + dx, layout.bounds.y0 + dy, layout.bounds.x1 + dx, layout.bounds.y1 + dy};
    }
    if (!layout.empty()) {
        e.setLayout(std::move(layout));
    }
    changed();
}

void EditMode::setActiveWidth(float width) {
    if (!active_) {
        return;
    }
    edit::ParagraphGeometry g = active_->editor.geometry();
    width = std::max(kMinWidth, width);
    if (std::abs(g.width - width) < 0.01f) {
        return;
    }
    g.width = width;
    active_->editor.setGeometry(g);
    changed();
}

void EditMode::paintHandles(QPainter& painter) const {
    const std::optional<QRectF> frame = activeFrame();
    if (!frame || locked()) {
        return;
    }
    const Palette& colors = Theme::colors();
    const Metrics& m = Theme::sizes();
    const QRectF r = frame->adjusted(-2, -2, 2, 2);
    const double h = m.handle;
    painter.setPen(QPen(colors.accent.color(), 1));
    painter.setBrush(colors.surface.color());
    for (const QPointF c : {QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y())}) {
        painter.drawRect(QRectF(c.x() - h / 2, c.y() - h / 2, h, h));
    }
}

}  // namespace ot::ui
