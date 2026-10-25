// «Установки → Измерения (2D)» (план 5.5, п. 2; эталон 3.4 «Измерения», 3.18; comment-29, comment-30):
// масштаб — «Использовать масштаб и единицы документа (если заданы)», масштаб и точность пользователя
// (ScaleFields); привязка — к вершинам и концам линий, ортогонально; пометка расстояния — концы линии
// (стрелки, засечки, нет), выносные линии (длина, продолжение, отступ, пт), подпись в разрыве линии или над
// ней; окно «Сведения об измерении». Ключи measuring/… (services/settings_keys.h); на работающее окно —
// MeasureTool::settingsChanged.
#pragma once

#include <QList>

#include "ui/shell/preferences/preferences_page.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

namespace ot::ui {

class PropertySheet;
class ScaleFields;

class MeasuringPage : public PreferencesPage {
    Q_OBJECT
public:
    explicit MeasuringPage(QWidget* parent = nullptr);

    QString categoryId() const override { return QStringLiteral("measuring"); }
    QString title() const override;
    void load(const Settings& settings) override;
    void apply(Settings& settings) override;

    QCheckBox* documentScaleBox() const { return documentScale_; }
    ScaleFields* scaleFields() const { return scale_; }
    QCheckBox* snapBox() const { return snap_; }
    QCheckBox* orthoBox() const { return ortho_; }
    QComboBox* lineEndsBox() const { return lineEnds_; }
    QSpinBox* leaderLengthBox() const { return leaderLength_; }
    QSpinBox* leaderExtensionBox() const { return leaderExtension_; }
    QSpinBox* leaderOffsetBox() const { return leaderOffset_; }
    QComboBox* captionBox() const { return caption_; }
    QCheckBox* infoBox() const { return info_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();
    QSpinBox* pointsBox(const char* name, int max);

    PropertySheet* sheet_ = nullptr;
    QCheckBox* documentScale_ = nullptr;
    ScaleFields* scale_ = nullptr;
    QCheckBox* snap_ = nullptr;
    QCheckBox* ortho_ = nullptr;
    QComboBox* lineEnds_ = nullptr;
    QSpinBox* leaderLength_ = nullptr;
    QSpinBox* leaderExtension_ = nullptr;
    QSpinBox* leaderOffset_ = nullptr;
    QComboBox* caption_ = nullptr;
    QCheckBox* info_ = nullptr;
    QList<QLabel*> sections_;
    QList<QLabel*> rows_;
};

}  // namespace ot::ui
