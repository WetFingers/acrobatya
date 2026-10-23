// Всплывающее уведомление (эталон 6.6: ошибка — тост danger 6 с с «Подробнее» → журнал): над нижним краем
// хоста по центру, тень, исчезает само; клик по «Подробнее» — actionTriggered.
#pragma once

#include <QFrame>

#include "ui/widgets/tone.h"

class QLabel;
class QPushButton;
class QTimer;

namespace ot::ui {

class ToolButton;

class Toast : public QFrame {
    Q_OBJECT
    Q_PROPERTY(QString tone READ toneProperty)
public:
    static constexpr int kDefaultMs = 6000;

    explicit Toast(const QString& text = {}, Tone tone = Tone::Danger, QWidget* parent = nullptr);

    // Показать над host (поверх его детей) и закрыть через msec; host владеет тостом.
    static Toast* notify(QWidget* host, const QString& text, Tone tone = Tone::Danger,
                         const QString& actionText = {}, int msec = kDefaultMs);

    void setText(const QString& text);
    QString text() const;
    void setActionText(const QString& text);
    void setDuration(int msec) { duration_ = msec; }
    int duration() const { return duration_; }
    // Показать над родителем и запустить таймер (0 — без таймера, для витрины).
    void popup();
    void dismiss();

signals:
    void actionTriggered();
    void closed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QString toneProperty() const { return toneName(tone_); }
    void place();
    void applyMetrics();

    QLabel* icon_ = nullptr;
    QLabel* text_ = nullptr;
    QPushButton* action_ = nullptr;
    ToolButton* close_ = nullptr;
    QTimer* timer_ = nullptr;
    Tone tone_ = Tone::Danger;
    int duration_ = kDefaultMs;
    bool popped_ = false;  // показан над хостом (а не встроен в раскладку, как в витрине)
};

}  // namespace ot::ui
