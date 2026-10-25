#include "ui/shell/message_bar.h"

#include <QPushButton>
#include <QVBoxLayout>

#include "ui/theme/theme.h"
#include "ui/widgets/message_bar.h"

namespace ot::ui {

namespace {

constexpr char kKindProperty[] = "otMessageKind";

}  // namespace

MessageStack::MessageStack(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("messageStack"));
    layout_ = new QVBoxLayout(this);
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &MessageStack::applyMetrics);
    hide();
}

void MessageStack::applyMetrics() {
    const Metrics& m = Theme::sizes();
    layout_->setContentsMargins(m.grid * 2, m.grid * 2, m.grid * 2, 0);
    layout_->setSpacing(m.grid);
}

Tone MessageStack::toneOf(MessageKind kind) {
    switch (kind) {
        case MessageKind::ProtectedView:
        case MessageKind::Repaired:
        case MessageKind::JavaScriptOff:
        case MessageKind::Xfa:
        case MessageKind::SignatureWarning:
            return Tone::Warn;
        case MessageKind::SignatureInvalid:
            return Tone::Danger;
        case MessageKind::Form:
            return Tone::Form;
        case MessageKind::SignatureValid:
        case MessageKind::PdfA:
        case MessageKind::LooksScanned:
        case MessageKind::ViewOnly:
        case MessageKind::Custom:
            break;
    }
    return Tone::Info;
}

int MessageStack::priorityOf(MessageKind kind) { return static_cast<int>(kind); }

MessageKind MessageStack::kindOf(const MessageBar* bar) {
    return static_cast<MessageKind>(bar->property(kKindProperty).toInt());
}

MessageBar* MessageStack::bar(MessageKind kind) const {
    if (kind == MessageKind::Custom) {
        return nullptr;
    }
    for (MessageBar* b : bars_) {
        if (kindOf(b) == kind) {
            return b;
        }
    }
    return nullptr;
}

MessageBar* MessageStack::showMessage(MessageKind kind, const QString& text) {
    if (MessageBar* existing = bar(kind)) {
        existing->setText(text);
        for (QPushButton* b : existing->findChildren<QPushButton*>()) {
            b->deleteLater();  // кнопки — заново у показавшего
        }
        return existing;
    }
    return insert(kind, toneOf(kind), text);
}

MessageBar* MessageStack::add(Tone tone, const QString& text) {
    return insert(MessageKind::Custom, tone, text);
}

MessageBar* MessageStack::insert(MessageKind kind, Tone tone, const QString& text) {
    auto* b = new MessageBar(tone, text, this);
    b->setProperty(kKindProperty, static_cast<int>(kind));
    b->setClosable(true);
    connect(b, &MessageBar::closed, this, [this, b] { removeBar(b); });
    // После всех полос не ниже по важности: равные — по порядку показа.
    int at = 0;
    while (at < bars_.size() && priorityOf(kindOf(bars_[at])) <= priorityOf(kind)) {
        ++at;
    }
    bars_.insert(at, b);
    layout_->insertWidget(at, b);
    QWidget::show();
    emit changed();
    return b;
}

void MessageStack::remove(MessageKind kind) {
    if (MessageBar* b = bar(kind)) {
        removeBar(b);
    }
}

void MessageStack::removeBar(MessageBar* b) {
    if (!bars_.removeOne(b)) {
        return;
    }
    layout_->removeWidget(b);
    b->hide();
    b->deleteLater();
    setVisible(!bars_.isEmpty());
    emit changed();
}

QList<MessageBar*> MessageStack::bars() const { return bars_; }

}  // namespace ot::ui
