#include "ui/shell/preferences/measuring_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include "services/measure_ops.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "ui/tools/measure/measure_scale_dialog.h"
#include "ui/widgets/property_sheet.h"

namespace ot::ui {

namespace {

constexpr const char* kLineEnds[] = {"arrow", "tick", "none"};
constexpr const char* kCaptions[] = {"inline", "top"};

}  // namespace

MeasuringPage::MeasuringPage(QWidget* parent) : PreferencesPage(parent) {
    setObjectName(QStringLiteral("measuringPage"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    sheet_ = new PropertySheet(this);
    column->addWidget(sheet_);
    column->addStretch(1);

    documentScale_ = new QCheckBox();
    documentScale_->setObjectName(QStringLiteral("measureDocumentScaleBox"));
    scale_ = new ScaleFields();
    snap_ = new QCheckBox();
    snap_->setObjectName(QStringLiteral("measureSnapBox"));
    ortho_ = new QCheckBox();
    ortho_->setObjectName(QStringLiteral("measureOrthoBox"));
    lineEnds_ = new QComboBox();
    lineEnds_->setObjectName(QStringLiteral("measureLineEndsBox"));
    fillData(lineEnds_, kLineEnds);
    leaderLength_ = pointsBox("measureLeaderLengthBox", settings::kMeasureLeaderLength.max);
    leaderExtension_ = pointsBox("measureLeaderExtensionBox", settings::kMeasureLeaderExtension.max);
    leaderOffset_ = pointsBox("measureLeaderOffsetBox", settings::kMeasureLeaderOffset.max);
    caption_ = new QComboBox();
    caption_->setObjectName(QStringLiteral("measureCaptionBox"));
    fillData(caption_, kCaptions);
    info_ = new QCheckBox();
    info_->setObjectName(QStringLiteral("measureInfoBox"));

    sections_ << sheet_->addSection(QString());
    sheet_->addWidget(documentScale_);
    sheet_->addWidget(scale_);
    sections_ << sheet_->addSection(QString());
    sheet_->addWidget(snap_);
    sheet_->addWidget(ortho_);
    sections_ << sheet_->addSection(QString());
    rows_ << sheet_->addRow(QString(), lineEnds_);
    rows_ << sheet_->addRow(QString(), leaderLength_);
    rows_ << sheet_->addRow(QString(), leaderExtension_);
    rows_ << sheet_->addRow(QString(), leaderOffset_);
    rows_ << sheet_->addRow(QString(), caption_);
    sections_ << sheet_->addSection(QString());
    sheet_->addWidget(info_);
    retranslate();
}

QSpinBox* MeasuringPage::pointsBox(const char* name, int max) {
    auto* box = new QSpinBox();
    box->setObjectName(QLatin1String(name));
    box->setRange(0, max);
    return box;
}

QString MeasuringPage::title() const { return tr("Измерения (2D)"); }

void MeasuringPage::load(const Settings& settings) {
    const measure::MeasureSettings s = measure::settings(settings);
    documentScale_->setChecked(s.useDocumentScale);
    scale_->setScale(s.scale);
    snap_->setChecked(s.snap);
    ortho_->setChecked(s.ortho);
    selectData(lineEnds_, settings.get(settings::kMeasureLineEnds));
    leaderLength_->setValue(settings.get(settings::kMeasureLeaderLength));
    leaderExtension_->setValue(settings.get(settings::kMeasureLeaderExtension));
    leaderOffset_->setValue(settings.get(settings::kMeasureLeaderOffset));
    selectData(caption_, settings.get(settings::kMeasureCaption));
    info_->setChecked(s.showInfo);
}

void MeasuringPage::apply(Settings& settings) {
    settings.set(settings::kMeasureUseDocumentScale, documentScale_->isChecked());
    const measure::Scale scale = scale_->scale();
    if (scale.valid()) {
        measure::setScale(settings, scale);
    }
    settings.set(settings::kMeasureSnap, snap_->isChecked());
    settings.set(settings::kMeasureOrtho, ortho_->isChecked());
    settings.set(settings::kMeasureLineEnds, lineEnds_->currentData().toString());
    settings.set(settings::kMeasureLeaderLength, leaderLength_->value());
    settings.set(settings::kMeasureLeaderExtension, leaderExtension_->value());
    settings.set(settings::kMeasureLeaderOffset, leaderOffset_->value());
    settings.set(settings::kMeasureCaption, caption_->currentData().toString());
    settings.set(settings::kMeasureShowInfo, info_->isChecked());
}

void MeasuringPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    PreferencesPage::changeEvent(event);
}

void MeasuringPage::retranslate() {
    const QStringList sections = {tr("Масштаб"), tr("Привязка"), tr("Пометка расстояния"),
                                  tr("Сведения об измерении")};
    for (int i = 0; i < sections_.size(); ++i) {
        sections_[i]->setText(sections.value(i));
    }
    const QStringList rows = {tr("&Концы линии:"), tr("&Длина выносной линии, пт:"),
                              tr("&Продолжение выносной линии, пт:"), tr("&Отступ выносной линии, пт:"),
                              tr("По&дпись:")};
    for (int i = 0; i < rows_.size(); ++i) {
        rows_[i]->setText(rows.value(i));
    }
    documentScale_->setText(tr("Использовать &масштаб и единицы документа (если заданы)"));
    snap_->setText(tr("Привязка к &вершинам и концам линий"));
    ortho_->setText(tr("&Ортогональная привязка (по горизонтали и вертикали)"));
    info_->setText(tr("Показывать окно «Сведения об &измерении»"));
    const QStringList ends = {tr("Стрелки"), tr("Засечки"), tr("Нет")};
    for (int i = 0; i < lineEnds_->count(); ++i) {
        lineEnds_->setItemText(i, ends.value(i));
    }
    const QStringList captions = {tr("В разрыве линии"), tr("Над линией")};
    for (int i = 0; i < caption_->count(); ++i) {
        caption_->setItemText(i, captions.value(i));
    }
}

}  // namespace ot::ui
