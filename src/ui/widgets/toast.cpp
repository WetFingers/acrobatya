#include "ui/widgets/toast.h"

#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

#include "ui/theme/theme.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

Toast::Toast(const QString& text, Tone tone, QWidget* parent) : QFrame(parent), tone_(tone) {
    auto* row = new QHBoxLayout(this);
    icon_ = new QLabel(this);
    text_ = new QLabel(text, this);
    text_->setObjectName(QStringLiteral("toastText"));
    text_->setWordWrap(true);
    action_ = new QPushButton(this);
    action_->setObjectName(QStringLiteral("toastAction"));
    action_->setFlat(true);
    action_->setCursor(Qt::PointingHandCursor);
    action_->hide();
    connect(action_, &QPushButton::clicked, this, &Toast::actionTriggered);
    close_ = new ToolButton(QStringLiteral("x"), tr("Закрыть"), this);
    close_->setIconSizeKind(ToolButton::IconSize::Row);
    connect(close_, &QToolButton::clicked, this, &Toast::dismiss);
    row->addWidget(icon_);
    row->addWidget(text_, 1);
    row->addWidget(action_);
    row->addWidget(close_);
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &Toast::dismiss);
    setAccessibleName(text);
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &Toast::applyMetrics);
}

Toast* Toast::notify(QWidget* host, const QString& text, Tone tone, const QString& actionText, int msec) {
    auto* toast = new Toast(text, tone, host);
    toast->setActionText(actionText);
    toast->setDuration(msec);
    toast->setAttribute(Qt::WA_DeleteOnClose);
    toast->popup();
    return toast;
}

void Toast::setText(const QString& text) {
    text_->setText(text);
    setAccessibleName(text);
}

QString Toast::text() const { return text_->text(); }

void Toast::setActionText(const QString& text) {
    action_->setText(text);
    action_->setVisible(!text.isEmpty());
}

void Toast::popup() {
    popped_ = true;
    if (QWidget* host = parentWidget()) {
        host->installEventFilter(this);
    }
    place();
    show();
    raise();
    if (duration_ > 0) {
        timer_->start(duration_);
    }
}

void Toast::dismiss() {
    timer_->stop();
    if (QWidget* host = parentWidget()) {
        host->removeEventFilter(this);
    }
    popped_ = false;
    hide();
    emit closed();
    if (testAttribute(Qt::WA_DeleteOnClose)) {
        deleteLater();
    }
}

bool Toast::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        place();
    }
    return QFrame::eventFilter(watched, event);
}

void Toast::place() {
    QWidget* host = parentWidget();
    if (!popped_ || host == nullptr || isWindow()) {
        return;
    }
    const Metrics& m = Theme::sizes();
    const int width = std::min(host->width() - m.grid * 8, m.leftPanel * 2);
    setFixedWidth(std::max(width, m.leftPanel));
    adjustSize();
    move((host->width() - this->width()) / 2, host->height() - height() - m.grid * 6);
}

void Toast::applyMetrics() {
    const Metrics& m = Theme::sizes();
    const Palette& p = Theme::colors();
    layout()->setContentsMargins(m.grid * 3, m.grid * 2, m.grid, m.grid * 2);
    layout()->setSpacing(m.grid * 2);
    icon_->setPixmap(Icons::get(toneIconName(tone_), toneIcon(tone_), m.iconPanel, devicePixelRatioF()));
    if (p.shadow.a == 0) {
        setGraphicsEffect(nullptr);
    } else {
        auto* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(m.shadowBlur);
        shadow->setOffset(0, m.shadowOffset);
        shadow->setColor(p.shadow.color());
        setGraphicsEffect(shadow);
    }
    if (isVisible()) {
        place();
    }
}

}  // namespace ot::ui
