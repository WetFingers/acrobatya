// Заголовок панели (эталон 6.1, 6.5): «◀ Все инструменты» (необязательно), название 13 px/600 и кнопки
// справа.
#pragma once

#include <QWidget>

class QHBoxLayout;
class QLabel;

namespace ot::ui {

class ToolButton;

class PanelHeader : public QWidget {
    Q_OBJECT
public:
    explicit PanelHeader(const QString& title = {}, QWidget* parent = nullptr);

    void setTitle(const QString& title);
    QString title() const;
    // Текст кнопки «назад»; пусто — кнопки нет.
    void setBackText(const QString& text);
    ToolButton* backButton() const { return back_; }
    ToolButton* addAction(const QString& iconName, const QString& toolTip);

signals:
    void backClicked();

private:
    void applyMetrics();

    ToolButton* back_ = nullptr;
    QLabel* title_ = nullptr;
    QHBoxLayout* actions_ = nullptr;
};

}  // namespace ot::ui
