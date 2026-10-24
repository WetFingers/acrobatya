#include "ui/tools/comment/quick_actions_bar.h"

#include <QEvent>
#include <QGuiApplication>
#include <QScrollBar>

#include <algorithm>

#include "ui/theme/theme.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/tools/comment/comment_tool.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

QuickActionsBar::QuickActionsBar(PageView& view) : FloatingBar(view.viewport()), view_(view) {
    setObjectName(QStringLiteral("quickActionsBar"));
    setAccessibleName(tr("Действия с выделенным текстом"));
    addButton(QStringLiteral("highlight"), QStringLiteral("highlighter"), tr("Выделить текст"), false);
    addButton(QStringLiteral("underline"), QStringLiteral("underline"), tr("Подчеркнуть текст"), false);
    addButton(QStringLiteral("strikeout"), QStringLiteral("strikethrough"), tr("Зачеркнуть текст"), false);
    addButton(QStringLiteral("note"), QStringLiteral("message-square-plus"),
              tr("Добавить комментарий к выделенному"), false);
    addSeparator();
    addButton(QStringLiteral("copy"), QStringLiteral("copy"), tr("Копировать текст"), false);
    connect(this, &FloatingBar::triggered, this, &QuickActionsBar::run);
    ToolController& tools = view_.tools();
    connect(&tools, &ToolController::selectionChanged, this, &QuickActionsBar::sync);
    connect(&tools, &ToolController::toolChanged, this, &QuickActionsBar::sync);
    connect(view_.horizontalScrollBar(), &QScrollBar::valueChanged, this, &QuickActionsBar::sync);
    connect(view_.verticalScrollBar(), &QScrollBar::valueChanged, this, &QuickActionsBar::sync);
    connect(&view_, &PageView::zoomChanged, this, &QuickActionsBar::sync);
    view_.viewport()->installEventFilter(this);
    hide();
}

bool QuickActionsBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == view_.viewport() &&
        (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::Resize)) {
        QMetaObject::invokeMethod(this, &QuickActionsBar::sync, Qt::QueuedConnection);
    }
    return FloatingBar::eventFilter(watched, event);
}

void QuickActionsBar::sync() {
    ToolController& tools = view_.tools();
    const bool show = tools.tool() == ViewToolId::Select && tools.select().hasSelection() &&
                      QGuiApplication::mouseButtons() == Qt::NoButton;
    if (!show) {
        hide();
        return;
    }
    const bool editable = tools.editable();
    for (const char* id : {"highlight", "underline", "strikeout", "note"}) {
        if (ToolButton* b = button(QLatin1String(id))) {
            b->setEnabled(editable);
        }
    }
    if (ToolButton* b = button(QStringLiteral("copy"))) {
        b->setEnabled(tools.copyAllowed());
    }
    // Над первым видимым квадом выделения; нет места сверху — под последним.
    std::optional<QRectF> first;
    std::optional<QRectF> last;
    for (const auto& [page, sel] : tools.select().selection()) {
        if (view_.pageViewportRect(page).isEmpty()) {
            continue;
        }
        const QTransform toView = view_.pageTransform(page);
        for (const QuadF& q : sel.quads) {
            const RectF b = q.bounds();
            const QRectF r = toView.mapRect(QRectF(QPointF(b.x0, b.y0), QPointF(b.x1, b.y1)));
            if (!first) {
                first = r;
            }
            last = r;
        }
    }
    if (!first) {
        hide();
        return;
    }
    adjustSize();
    const int gap = Theme::sizes().grid * 2;
    const QRect area = view_.viewport()->rect();
    int x = static_cast<int>(first->left());
    int y = static_cast<int>(first->top()) - height() - gap;
    if (y < area.top()) {
        x = static_cast<int>(last->left());
        y = static_cast<int>(last->bottom()) + gap;
    }
    x = std::clamp(x, area.left(), std::max(area.left(), area.right() - width()));
    y = std::clamp(y, area.top(), std::max(area.top(), area.bottom() - height()));
    move(x, y);
    QWidget::show();
    raise();
}

void QuickActionsBar::run(const QString& id) {
    ToolController& tools = view_.tools();
    if (id == QLatin1String("copy")) {
        tools.select().copySelection();
        return;
    }
    if (!tools.editable() || !tools.select().hasSelection()) {
        return;
    }
    CommentAction action = CommentAction::Highlight;
    if (id == QLatin1String("underline")) {
        action = CommentAction::Underline;
    } else if (id == QLatin1String("strikeout")) {
        action = CommentAction::StrikeOut;
    }
    const auto selection = tools.select().selection();
    CommentTool::modeOf(view_).markupSelection(action, selection, id == QLatin1String("note"));
    tools.select().clearSelection();
    hide();
}

}  // namespace ot::ui
