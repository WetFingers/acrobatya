// Чип (эталон 6.4: статусы подписей, фильтры комментариев): «таблетка» с тоном, иконкой и подписью;
// закрываемый (крестик → closeRequested) или переключаемый фильтр (checked — accentSoft и рамка accent).
#pragma once

#include <QFrame>

#include "ui/widgets/tone.h"

class QLabel;

namespace ot::ui {

class ToolButton;

class Chip : public QFrame {
    Q_OBJECT
    Q_PROPERTY(QString tone READ toneProperty)
    Q_PROPERTY(bool checkable READ isCheckable WRITE setCheckable)
    Q_PROPERTY(bool checked READ isChecked WRITE setChecked NOTIFY toggled)
public:
    explicit Chip(const QString& text = {}, Tone tone = Tone::Neutral, QWidget* parent = nullptr);

    void setText(const QString& text);
    QString text() const;
    void setTone(Tone tone);
    Tone tone() const { return tone_; }
    void setIconName(const QString& name);
    void setClosable(bool closable);
    void setCheckable(bool checkable);
    bool isCheckable() const { return checkable_; }
    void setChecked(bool checked);
    bool isChecked() const { return checked_; }
    ToolButton* closeButton() const { return close_; }

signals:
    void clicked();
    void toggled(bool checked);
    void closeRequested();

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QString toneProperty() const { return toneName(tone_); }
    void activate();
    void applyMetrics();

    QLabel* icon_ = nullptr;
    QLabel* label_ = nullptr;
    ToolButton* close_ = nullptr;
    QString iconName_;
    Tone tone_ = Tone::Neutral;
    bool checkable_ = false;
    bool checked_ = false;
};

}  // namespace ot::ui
