#include "ui/shell/quick_bar.h"

#include <QCoreApplication>
#include <QEvent>

#include "services/settings.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

namespace {

QString itemTitle(const QuickBar::Item& item) {
    return QCoreApplication::translate("ot::ui::QuickBar", item.title);
}

}  // namespace

const std::vector<QuickBar::Item>& QuickBar::items() {
    static const std::vector<Item> list = {
        {"select", "mouse-pointer-2", QT_TRANSLATE_NOOP("ot::ui::QuickBar", "Выделение"), 0, "V"},
        {"hand", "hand", QT_TRANSLATE_NOOP("ot::ui::QuickBar", "Рука"), 0, "H"},
        {"note", "sticky-note", QT_TRANSLATE_NOOP("ot::ui::QuickBar", "Записка"), 5, "S"},
        {"highlight", "highlighter", QT_TRANSLATE_NOOP("ot::ui::QuickBar", "Выделить текст"), 5, "U"},
        {"draw", "pencil", QT_TRANSLATE_NOOP("ot::ui::QuickBar", "Рисовать"), 5, "D"},
        {"text", "type", QT_TRANSLATE_NOOP("ot::ui::QuickBar", "Добавить текст"), 5, "J"},
        {"sign", "signature", QT_TRANSLATE_NOOP("ot::ui::QuickBar", "Подпись"), 5, ""},
        {"zoom", "zoom-in", QT_TRANSLATE_NOOP("ot::ui::QuickBar", "Масштаб рамкой"), 0, "Z"},
    };
    return list;
}

QStringList QuickBar::defaultOrder() {
    return {QStringLiteral("select"), QStringLiteral("hand"),      QLatin1String(kSeparator),
            QStringLiteral("note"),   QStringLiteral("highlight"), QStringLiteral("draw"),
            QStringLiteral("text"),   QStringLiteral("sign"),      QLatin1String(kSeparator),
            QStringLiteral("zoom")};
}

QuickBar::QuickBar(QWidget* parent) : FloatingBar(parent) {
    setObjectName(QStringLiteral("quickBar"));
    for (const Item& item : items()) {
        ToolButton* b = addButton(QLatin1String(item.id), QLatin1String(item.icon), QString());
        b->setEnabled(item.stage == 0);
    }
    const QStringList order = Settings().get(settings::kQuickBarOrder);
    setOrder(order.isEmpty() ? defaultOrder() : order);
    connect(this, &FloatingBar::triggered, this, &QuickBar::trigger);
    retranslate();
}

void QuickBar::attach(PageView* view) {
    view_ = view;
    if (view == nullptr) {
        return;
    }
    connect(&view->tools(), &ToolController::toolChanged, this, &QuickBar::sync);
    sync(view->tools().tool());
}

void QuickBar::setSingleKeyHints(bool on) {
    keyHints_ = on;
    retranslate();
}

void QuickBar::retranslate() {
    for (const Item& item : items()) {
        ToolButton* b = button(QLatin1String(item.id));
        QString tip = itemTitle(item);
        if (keyHints_ && item.key[0] != '\0') {
            tip += QStringLiteral(" (%1)").arg(QLatin1String(item.key));
        }
        b->setLabel(itemTitle(item));
        b->setToolTip(
            item.stage == 0 ? tip : tr("«%1» появится в версии 0.%2").arg(itemTitle(item)).arg(item.stage));
    }
    setAccessibleName(tr("Панель быстрых действий"));
}

void QuickBar::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    FloatingBar::changeEvent(event);
}

void QuickBar::sync(ViewToolId tool) {
    switch (tool) {
        case ViewToolId::Select:
            setCurrent(QStringLiteral("select"));
            break;
        case ViewToolId::Hand:
            setCurrent(QStringLiteral("hand"));
            break;
        case ViewToolId::MarqueeZoom:
        case ViewToolId::Loupe:
        case ViewToolId::PanZoom:
            setCurrent(QStringLiteral("zoom"));
            break;
        case ViewToolId::Snapshot:
        case ViewToolId::AnnotSelect:
        case ViewToolId::Edit:
        case ViewToolId::Sign:
        case ViewToolId::Comment:
        case ViewToolId::FillSign:
        case ViewToolId::Measure:
            break;
    }
}

void QuickBar::trigger(const QString& id) {
    if (view_ == nullptr) {
        return;
    }
    if (id == QLatin1String("select")) {
        view_->tools().setTool(ViewToolId::Select);
    } else if (id == QLatin1String("hand")) {
        view_->tools().setTool(ViewToolId::Hand);
    } else if (id == QLatin1String("zoom")) {
        view_->tools().setTool(ViewToolId::MarqueeZoom);
    }
    view_->setFocus();
}

}  // namespace ot::ui
