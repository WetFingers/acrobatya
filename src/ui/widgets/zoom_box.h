// Масштаб в строке состояния (эталон 6.1: «− [125 % ▾] +»): предустановки процентов и «Уместить страницу»,
// «По ширине», «Видимое»; ввод «150», «150 %», «12,5» → 1–6400 %; −/+ — соседняя предустановка.
#pragma once

#include <QList>
#include <QWidget>

#include <optional>

class QComboBox;

namespace ot::ui {

class ToolButton;

class ZoomBox : public QWidget {
    Q_OBJECT
public:
    enum class Fit { Page, Width, Visible };
    static constexpr double kMinPercent = 1.0;
    static constexpr double kMaxPercent = 6400.0;

    explicit ZoomBox(QWidget* parent = nullptr);

    void setZoomPercent(double percent);  // показать, без сигнала
    double zoomPercent() const { return percent_; }
    QComboBox* combo() const { return combo_; }
    ToolButton* zoomInButton() const { return in_; }
    ToolButton* zoomOutButton() const { return out_; }

    static const QList<double>& presets();
    static QString formatPercent(double percent);
    static std::optional<double> parsePercent(const QString& text);
    // Соседняя предустановка: direction > 0 — больше текущего, < 0 — меньше; на краях — край диапазона.
    static double stepPreset(double current, int direction);

public slots:
    void zoomIn();
    void zoomOut();

signals:
    void zoomRequested(double percent);
    void fitRequested(ot::ui::ZoomBox::Fit fit);

private:
    void request(double percent);
    void commitText();
    void applyMetrics();

    QComboBox* combo_ = nullptr;
    ToolButton* in_ = nullptr;
    ToolButton* out_ = nullptr;
    double percent_ = 100.0;
};

}  // namespace ot::ui
