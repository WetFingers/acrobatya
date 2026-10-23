#include "ui/widgets/color_swatch.h"

#include <QColorDialog>
#include <QGridLayout>
#include <QMenu>
#include <QPainter>
#include <QWidgetAction>

#include "ui/theme/theme.h"
#include "ui/theme/tokens.h"

namespace ot::ui {

ColorSwatch::ColorSwatch(const QColor& color, QWidget* parent)
    : QToolButton(parent), color_(color.isValid() ? color : kAnnotationColors[0].color()) {
    setFocusPolicy(Qt::TabFocus);
    setPopupMode(QToolButton::InstantPopup);
    setToolTip(tr("Цвет"));
    setAccessibleName(tr("Цвет"));
    menu_ = new QMenu(this);
    auto* grid = new QWidget(menu_);
    auto* layout = new QGridLayout(grid);
    int i = 0;
    for (const Rgba& c : kAnnotationColors) {
        auto* b = new QToolButton(grid);
        b->setObjectName(QStringLiteral("colorPreset"));
        b->setAutoRaise(true);
        b->setAccessibleName(c.color().name());
        b->setToolTip(c.color().name());
        const QColor qc = c.color();
        connect(b, &QToolButton::clicked, this, [this, qc] {
            menu_->close();
            choose(qc);
        });
        layout->addWidget(b, i / 6, i % 6);
        presets_.push_back(b);
        ++i;
    }
    auto* gridAction = new QWidgetAction(menu_);
    gridAction->setDefaultWidget(grid);
    menu_->addAction(gridAction);
    menu_->addSeparator();
    menu_->addAction(tr("Другой цвет…"), this, &ColorSwatch::chooseOther);
    setMenu(menu_);
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &ColorSwatch::applyMetrics);
}

void ColorSwatch::setColor(const QColor& color) {
    color_ = color;
    applyMetrics();
}

void ColorSwatch::choose(const QColor& color) {
    if (!color.isValid() || color == color_) {
        return;
    }
    setColor(color);
    emit colorChanged(color_);
}

void ColorSwatch::chooseOther() { choose(QColorDialog::getColor(color_, this, tr("Другой цвет"))); }

QPixmap ColorSwatch::swatchPixmap(const QColor& color, int side) const {
    const qreal dpr = devicePixelRatioF();
    QPixmap pm(qRound(side * dpr), qRound(side * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Theme::colors().border.color(), 1));
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(0.5, 0.5, side - 1, side - 1), 2, 2);
    return pm;
}

void ColorSwatch::applyMetrics() {
    const Metrics& m = Theme::sizes();
    setIconSize(QSize(m.iconPanel, m.iconPanel));
    setIcon(QIcon(swatchPixmap(color_, m.iconPanel)));
    setMinimumSize(m.hitTarget + m.grid * 2, m.hitTarget);
    for (size_t i = 0; i < presets_.size(); ++i) {
        presets_[i]->setIconSize(QSize(m.iconPanel, m.iconPanel));
        presets_[i]->setIcon(QIcon(swatchPixmap(kAnnotationColors[i].color(), m.iconPanel)));
        presets_[i]->setMinimumSize(m.hitTarget, m.hitTarget);
    }
}

}  // namespace ot::ui
