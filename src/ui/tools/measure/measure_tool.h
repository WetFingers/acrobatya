// Инструмент «Измерить» из «Все инструменты» (план 5.5, п. 2; эталон 3.4 «Измерения», 6.2 п. 14 — дубль
// «Измерить ▾» инструмента «Добавить комментарии»; comment-29, comment-30): панель слева 320 — Расстояние,
// Периметр, Площадь; масштаб («1 см = 10 м», масштаб документа — если он есть в точке измерения) с кнопками
// «Масштаб и точность…» (MeasureScaleDialog) и «Калибровать» (отрезок известной длины — тот же диалог с
// калибровкой); привязка к вершинам и концам путей, «Ортогонально», «Сведения об измерении» (окно
// MeasureInfoPanel). Флажки и масштаб — ключи measuring/… (Установки → Измерения (2D)). Режим вида —
// MeasureMode в слоте ViewToolId::Measure. Правка запрещена (защищённый просмотр, только чтение, не PDF,
// право /P) — кнопки серые, причина — строкой панели.
#pragma once

#include <QCheckBox>
#include <QLabel>
#include <QObject>
#include <QPointer>
#include <QString>

#include <map>
#include <vector>

#include "ui/tools/measure/measure_mode.h"
#include "ui/tools/tool.h"
#include "ui/widgets/tool_button.h"

class QVBoxLayout;

namespace ot::ui {

class MeasureScaleDialog;
class PageView;
class ToolRegistry;

class MeasureTool : public QObject, public Tool {
    Q_OBJECT
public:
    explicit MeasureTool(ToolRegistry& registry);
    ~MeasureTool() override;

    QString id() const override { return QStringLiteral("measure"); }
    QString title() const override;
    QString icon() const override;
    int stage() const override { return 5; }
    void activate(DocumentView& view) override;
    void deactivate() override;
    QWidget* panel() override;
    void settingsChanged() override;

    static MeasureMode& modeOf(PageView& view);
    MeasureMode* mode() const;
    DocumentView* view() const;
    QString lockReason() const { return lock_; }

    // Включить действие на вкладке инструмента.
    void start(MeasureAction action);
    ToolButton* actionButton(MeasureAction action) const;
    QLabel* lockLabel() const { return lockLabel_; }
    QLabel* scaleLabel() const { return scaleLabel_; }
    ToolButton* scaleButton() const { return scaleButton_; }
    QCheckBox* snapBox() const { return snap_; }
    QCheckBox* orthoBox() const { return ortho_; }
    QCheckBox* infoBox() const { return info_; }

    // «Масштаб и точность…» (calibrationPt > 0 — с калибровкой по отрезку); «ОК» пишет measuring/scale.
    MeasureScaleDialog* showScaleDialog(double calibrationPt = 0);

signals:
    void lockChanged(const QString& reason);

private:
    void buildPanel();
    void updateLock();
    void syncButtons();
    void updateScaleLabel();
    void reloadSettings();
    void saveFlags();
    ToolButton* addRow(QVBoxLayout* column, const QString& icon, const QString& label);
    void addCaption(QVBoxLayout* column, const QString& text);

    ToolRegistry& registry_;
    QPointer<DocumentView> view_;
    QPointer<QWidget> panel_;
    std::map<MeasureAction, QPointer<ToolButton>> buttons_;
    QPointer<QLabel> lockLabel_;
    QPointer<QLabel> scaleLabel_;
    QPointer<ToolButton> scaleButton_;
    QPointer<QCheckBox> snap_;
    QPointer<QCheckBox> ortho_;
    QPointer<QCheckBox> info_;
    QString lock_;
    std::vector<QMetaObject::Connection> connections_;
};

}  // namespace ot::ui
