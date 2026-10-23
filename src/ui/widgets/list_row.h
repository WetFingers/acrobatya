// Строка списка 32 px (эталон 6.2, 6.4: «Все инструменты», панели): иконка 16/20, текст, подпись справа,
// звёздочка «закрепить» (необязательно); наведение — hover, выбранная — accentSoft. Цвета состояний —
// rowColors() темы (их же проверяет тест контраста); рисуется сама, без QSS.
#pragma once

#include <QWidget>

namespace ot::ui {

class ToolButton;

class ListRow : public QWidget {
    Q_OBJECT
public:
    explicit ListRow(const QString& text = {}, const QString& iconName = {}, QWidget* parent = nullptr);

    void setText(const QString& text);
    QString text() const { return text_; }
    void setDetail(const QString& detail);
    QString detail() const { return detail_; }
    void setIconName(const QString& name);
    void setSelected(bool selected);
    bool isSelected() const { return selected_; }
    void setPinnable(bool pinnable);
    void setPinned(bool pinned);
    bool isPinned() const;
    ToolButton* pinButton() const { return pin_; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void clicked();
    void pinToggled(bool pinned);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void applyMetrics();

    QString text_;
    QString detail_;
    QString iconName_;
    bool selected_ = false;
    ToolButton* pin_ = nullptr;
};

}  // namespace ot::ui
