#include "ui/widgets/zoom_box.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLocale>

#include <algorithm>
#include <cmath>

#include "ui/theme/theme.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

namespace {

constexpr int kFitRole = Qt::UserRole + 1;
constexpr int kPercentRole = Qt::UserRole + 2;

}  // namespace

const QList<double>& ZoomBox::presets() {
    static const QList<double> list = {10, 25, 50, 75, 100, 125, 150, 200, 300, 400, 800, 1600, 3200, 6400};
    return list;
}

QString ZoomBox::formatPercent(double percent) {
    const double rounded = std::round(percent * 100.0) / 100.0;
    const bool whole = std::abs(rounded - std::round(rounded)) < 1e-9;
    return QLocale().toString(
               rounded, 'f', whole ? 0 : (std::abs(rounded * 10 - std::round(rounded * 10)) < 1e-9 ? 1 : 2)) +
           QChar(0x00A0) + QLatin1Char('%');
}

std::optional<double> ZoomBox::parsePercent(const QString& text) {
    QString t = text;
    t.remove(QLatin1Char('%')).remove(QChar(0x00A0)).remove(QLatin1Char(' '));
    t.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double v = t.toDouble(&ok);
    if (!ok || !std::isfinite(v) || v <= 0) {
        return std::nullopt;
    }
    return std::clamp(v, kMinPercent, kMaxPercent);
}

double ZoomBox::stepPreset(double current, int direction) {
    const QList<double>& list = presets();
    if (direction > 0) {
        for (double p : list) {
            if (p > current + 1e-6) {
                return p;
            }
        }
        return kMaxPercent;
    }
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        if (*it < current - 1e-6) {
            return *it;
        }
    }
    return std::min(current, list.front());
}

ZoomBox::ZoomBox(QWidget* parent) : QWidget(parent) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    out_ = new ToolButton(QStringLiteral("minus"), tr("Уменьшить"), this);
    in_ = new ToolButton(QStringLiteral("plus"), tr("Увеличить"), this);
    out_->setIconSizeKind(ToolButton::IconSize::Row);
    in_->setIconSizeKind(ToolButton::IconSize::Row);
    combo_ = new QComboBox(this);
    combo_->setEditable(true);
    combo_->setInsertPolicy(QComboBox::NoInsert);
    combo_->setAccessibleName(tr("Масштаб"));
    combo_->setToolTip(tr("Масштаб"));
    for (double p : presets()) {
        combo_->addItem(formatPercent(p));
        combo_->setItemData(combo_->count() - 1, p, kPercentRole);
    }
    combo_->insertSeparator(combo_->count());
    const std::pair<QString, Fit> fits[] = {
        {tr("Уместить страницу"), Fit::Page}, {tr("По ширине"), Fit::Width}, {tr("Видимое"), Fit::Visible}};
    for (const auto& [title, fit] : fits) {
        combo_->addItem(title);
        combo_->setItemData(combo_->count() - 1, static_cast<int>(fit), kFitRole);
    }
    row->addWidget(out_);
    row->addWidget(combo_);
    row->addWidget(in_);
    connect(out_, &QToolButton::clicked, this, &ZoomBox::zoomOut);
    connect(in_, &QToolButton::clicked, this, &ZoomBox::zoomIn);
    connect(combo_, &QComboBox::activated, this, [this](int index) {
        const QVariant fit = combo_->itemData(index, kFitRole);
        if (fit.isValid()) {
            combo_->setEditText(formatPercent(percent_));
            emit fitRequested(static_cast<Fit>(fit.toInt()));
            return;
        }
        const QVariant p = combo_->itemData(index, kPercentRole);
        if (p.isValid()) {
            request(p.toDouble());
        }
    });
    connect(combo_->lineEdit(), &QLineEdit::returnPressed, this, &ZoomBox::commitText);
    setZoomPercent(100.0);
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &ZoomBox::applyMetrics);
}

void ZoomBox::setZoomPercent(double percent) {
    percent_ = std::clamp(percent, kMinPercent, kMaxPercent);
    combo_->setEditText(formatPercent(percent_));
    out_->setEnabled(percent_ > kMinPercent);
    in_->setEnabled(percent_ < kMaxPercent);
}

void ZoomBox::zoomIn() { request(stepPreset(percent_, +1)); }

void ZoomBox::zoomOut() { request(stepPreset(percent_, -1)); }

void ZoomBox::request(double percent) {
    setZoomPercent(percent);
    emit zoomRequested(percent_);
}

void ZoomBox::commitText() {
    if (const auto p = parsePercent(combo_->currentText())) {
        request(*p);
    } else {
        setZoomPercent(percent_);
    }
}

void ZoomBox::applyMetrics() {
    const Metrics& m = Theme::sizes();
    layout()->setSpacing(m.grid / 2);
    combo_->setMinimumHeight(m.row - m.grid * 2);
    combo_->setFixedWidth(m.grid * 24);
}

}  // namespace ot::ui
