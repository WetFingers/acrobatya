#include "ui/widgets/floating_bar.h"

#include <QButtonGroup>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>

#include "ui/theme/theme.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

FloatingBar::FloatingBar(QWidget* parent) : QFrame(parent) {
    layout_ = new QHBoxLayout(this);
    group_ = new QButtonGroup(this);
    group_->setExclusive(true);
    setAccessibleName(tr("Панель быстрых действий"));
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &FloatingBar::applyMetrics);
}

ToolButton* FloatingBar::addButton(const QString& id, const QString& iconName, const QString& toolTip,
                                   bool checkable) {
    auto* b = new ToolButton(iconName, toolTip, this);
    b->setObjectName(QStringLiteral("floating_") + id);
    b->setCheckable(checkable);
    if (checkable) {
        group_->addButton(b);
    }
    connect(b, &QToolButton::clicked, this, [this, id] { emit triggered(id); });
    buttons_[id] = b;
    order_ << id;
    relayout();
    return b;
}

void FloatingBar::addWidget(const QString& id, QWidget* widget) {
    widget->setParent(this);
    widgets_[id] = widget;
    order_ << id;
    relayout();
}

void FloatingBar::addSeparator() {
    order_ << QLatin1String(kSeparator);
    relayout();
}

void FloatingBar::setOrder(const QStringList& ids) {
    order_.clear();
    for (const QString& id : ids) {
        if (id == QLatin1String(kSeparator) || buttons_.count(id) != 0 || widgets_.count(id) != 0) {
            order_ << id;
        }
    }
    relayout();
}

ToolButton* FloatingBar::button(const QString& id) const {
    const auto it = buttons_.find(id);
    return it == buttons_.end() ? nullptr : it->second;
}

void FloatingBar::setCurrent(const QString& id) {
    if (ToolButton* b = button(id); b != nullptr && b->isCheckable()) {
        b->setChecked(true);
    }
}

QString FloatingBar::current() const {
    for (const auto& [id, b] : buttons_) {
        if (b->isChecked()) {
            return id;
        }
    }
    return {};
}

void FloatingBar::relayout() {
    while (QLayoutItem* item = layout_->takeAt(0)) {
        delete item;
    }
    qDeleteAll(separators_);
    separators_.clear();
    for (const auto& [id, b] : buttons_) {
        b->setVisible(order_.contains(id));
    }
    for (const auto& [id, w] : widgets_) {
        w->setVisible(order_.contains(id));
    }
    const Metrics& m = Theme::sizes();
    for (const QString& id : order_) {
        if (id == QLatin1String(kSeparator)) {
            auto* sep = new QFrame(this);
            sep->setObjectName(QStringLiteral("floatingSeparator"));
            sep->setFixedSize(1, m.iconPanel);
            layout_->addWidget(sep);
            separators_ << sep;
        } else if (ToolButton* b = button(id)) {
            layout_->addWidget(b);
        } else if (const auto it = widgets_.find(id); it != widgets_.end()) {
            layout_->addWidget(it->second, 0, Qt::AlignVCenter);
        }
    }
    adjustSize();
}

void FloatingBar::applyMetrics() {
    const Metrics& m = Theme::sizes();
    const Palette& p = Theme::colors();
    layout_->setContentsMargins(m.grid, m.grid, m.grid, m.grid);
    layout_->setSpacing(m.grid / 2);
    for (QWidget* sep : separators_) {
        sep->setFixedSize(1, m.iconPanel);
    }
    if (p.shadow.a == 0) {
        setGraphicsEffect(nullptr);  // «Высокая контрастность» — рамка вместо тени
    } else {
        auto* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(m.shadowBlur);
        shadow->setOffset(0, m.shadowOffset);
        shadow->setColor(p.shadow.color());
        setGraphicsEffect(shadow);
    }
    adjustSize();
}

}  // namespace ot::ui
