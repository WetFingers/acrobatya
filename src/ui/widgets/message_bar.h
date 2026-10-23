// Полоса сообщений над документом (эталон 6.1, 6.6): тон (сведения — синяя, предупреждение — жёлтая, форма —
// фиолетовая, ошибка — красная, успех — зелёная), иконка, текст, кнопки действий и крестик. Стек полос —
// оболочка.
#pragma once

#include <QFrame>

#include "ui/widgets/tone.h"

class QHBoxLayout;
class QLabel;
class QPushButton;

namespace ot::ui {

class ToolButton;

class MessageBar : public QFrame {
    Q_OBJECT
    Q_PROPERTY(QString tone READ toneProperty)
public:
    explicit MessageBar(Tone tone = Tone::Info, const QString& text = {}, QWidget* parent = nullptr);

    void setTone(Tone tone);
    Tone tone() const { return tone_; }
    void setText(const QString& text);
    QString text() const;
    // Кнопка действия справа от текста; id — в actionTriggered.
    QPushButton* addAction(const QString& text, const QString& id, bool primary = false);
    void setClosable(bool closable);
    ToolButton* closeButton() const { return close_; }

signals:
    void actionTriggered(const QString& id);
    void closed();

private:
    QString toneProperty() const { return toneName(tone_); }
    void applyMetrics();

    QLabel* icon_ = nullptr;
    QLabel* text_ = nullptr;
    QHBoxLayout* actions_ = nullptr;
    ToolButton* close_ = nullptr;
    Tone tone_ = Tone::Info;
};

}  // namespace ot::ui
