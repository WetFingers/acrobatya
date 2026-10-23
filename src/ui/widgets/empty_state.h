// Пустое состояние (эталон 6.6): иконка + одна строка + действие — Главная без недавних, панель без
// комментариев.
#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace ot::ui {

class EmptyState : public QWidget {
    Q_OBJECT
public:
    explicit EmptyState(const QString& iconName = {}, const QString& text = {},
                        const QString& actionText = {}, QWidget* parent = nullptr);

    void setIconName(const QString& name);
    void setText(const QString& text);
    QString text() const;
    void setActionText(const QString& text);
    QPushButton* actionButton() const { return action_; }

signals:
    void actionTriggered();

private:
    void applyMetrics();

    QLabel* icon_ = nullptr;
    QLabel* text_ = nullptr;
    QPushButton* action_ = nullptr;
    QString iconName_;
};

}  // namespace ot::ui
