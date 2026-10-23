// Переключатель из нескольких сегментов (эталон 6.1: «Одна · Непрерывно · Разворот» в строке состояния):
// один выбранный, сигнал currentChanged при смене (и программной).
#pragma once

#include <QWidget>

#include <vector>

class QButtonGroup;
class QHBoxLayout;
class QToolButton;

namespace ot::ui {

class SegmentedBar : public QWidget {
    Q_OBJECT
public:
    explicit SegmentedBar(QWidget* parent = nullptr);

    // Сегмент с подписью и/или иконкой; без подписи — tip становится подсказкой и именем для доступности.
    int addSegment(const QString& text, const QString& iconName = {}, const QString& tip = {});
    int count() const { return static_cast<int>(buttons_.size()); }
    int currentIndex() const;
    void setCurrentIndex(int index);
    void setSegmentEnabled(int index, bool enabled);
    QToolButton* segment(int index) const;

signals:
    void currentChanged(int index);

private:
    void applyMetrics();

    QHBoxLayout* layout_ = nullptr;
    QButtonGroup* group_ = nullptr;
    std::vector<QToolButton*> buttons_;
    std::vector<QString> icons_;
};

}  // namespace ot::ui
