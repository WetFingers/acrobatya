// Плавающая панель (эталон 6.1: QuickBar внизу документа; основа cloud-38): скругление 8, тень 0 4 16,
// кнопки-иконки и разделители; один выбранный инструмент; состав и порядок задаются списком id («|» —
// разделитель) — его хранит настройка панели быстрых действий.
#pragma once

#include <QFrame>
#include <QStringList>

#include <map>

class QButtonGroup;
class QHBoxLayout;

namespace ot::ui {

class ToolButton;

class FloatingBar : public QFrame {
    Q_OBJECT
public:
    static constexpr auto kSeparator = "|";

    explicit FloatingBar(QWidget* parent = nullptr);

    // Кнопка известна панели; в раскладку попадает через setOrder (addButton без setOrder — в конец).
    ToolButton* addButton(const QString& id, const QString& iconName, const QString& toolTip,
                          bool checkable = true);
    // Не кнопка (поле страницы, подпись масштаба полосы режима чтения 2.4) — в раскладку по id, как кнопка.
    void addWidget(const QString& id, QWidget* widget);
    void addSeparator();
    void setOrder(const QStringList& ids);
    QStringList order() const { return order_; }
    ToolButton* button(const QString& id) const;
    void setCurrent(const QString& id);
    QString current() const;

signals:
    void triggered(const QString& id);

private:
    void relayout();
    void applyMetrics();

    QHBoxLayout* layout_ = nullptr;
    QButtonGroup* group_ = nullptr;
    std::map<QString, ToolButton*> buttons_;
    std::map<QString, QWidget*> widgets_;
    QStringList order_;
    QList<QWidget*> separators_;
};

}  // namespace ot::ui
