// Карточка (эталон 6.4: скругление 8, Главная, комментарии, подписи): иконка, заголовок, текст-подпись и своё
// содержимое снизу; кликабельная — рамка ярче при наведении, Enter/Space; выбранная — рамка accent.
#pragma once

#include <QFrame>

class QLabel;
class QVBoxLayout;

namespace ot::ui {

class Card : public QFrame {
    Q_OBJECT
    Q_PROPERTY(bool clickable READ isClickable WRITE setClickable)
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected)
public:
    explicit Card(const QString& title = {}, const QString& text = {}, const QString& iconName = {},
                  QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setText(const QString& text);
    void setIconName(const QString& name);
    void setClickable(bool clickable);
    bool isClickable() const { return clickable_; }
    void setSelected(bool selected);
    bool isSelected() const { return selected_; }
    QVBoxLayout* contentLayout() const { return content_; }

signals:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void applyMetrics();

    QLabel* icon_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* text_ = nullptr;
    QVBoxLayout* content_ = nullptr;
    QString iconName_;
    bool clickable_ = false;
    bool selected_ = false;
};

}  // namespace ot::ui
