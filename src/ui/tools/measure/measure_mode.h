// Режим «Измерить» вида (план 5.5, п. 2; эталон 3.4 «Измерения», 6.2 п. 3 и 14; comment-29, comment-30):
// инструмент вида в слоте ViewToolId::Measure (ставит MeasureTool), действие — MeasureAction:
//   Расстояние — два щелчка: /Line /LineDimension с подписью «3,53 м» на линии (/Cap), концами и выносными
//     линиями из Установок → Измерения (2D);
//   Периметр — щелчок на каждую вершину, Площадь — то же (≥ 3 вершин); конец — двойной щелчок, Enter или
//     правая кнопка (у площади — и щелчок в первую вершину), Backspace — убрать вершину, Esc — бросить;
//   Калибровка — два щелчка по отрезку известной длины: сигнал calibrationRequested (MeasureTool открывает
//     «Масштаб и точность» с калибровкой).
// Привязка — к вершинам и концам путей страницы (core/annot/measure.h snapPoints, кеш по страницам) в радиусе
// kSnapRadiusPx экрана, маркер-квадрат у привязанной точки; Shift — отрезок под 45°, «Ортогонально» — под
// 90°. Масштаб — документа (/VP страницы в первой точке, если «Использовать масштаб документа»), иначе
// пользователя (measuring/scale). Идущее измерение — пунктир с резинкой к курсору и подпись значения у
// курсора; reading() — значение, Δx/Δy и угол последнего отрезка, масштаб — для окна «Сведения об измерении»
// (MeasureInfoPanel, правый верхний угол вида). Правка запрещена (защищённый просмотр, только чтение, право
// /P) — setLocked: курсор «нельзя», щелчки поглощаются. Каждое измерение — шаг истории
// (services/measure_ops.h).
#pragma once

#include <QFuture>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QString>

#include <map>
#include <set>
#include <vector>

#include "core/annot/annots.h"
#include "core/annot/measure.h"
#include "services/measure_ops.h"
#include "ui/view/tools/view_tool.h"

namespace ot {
class DocumentSession;
}

namespace ot::ui {

class MeasureInfoPanel;

enum class MeasureAction { Distance, Perimeter, Area, Calibrate };

// Текущее измерение — для окна сведений.
struct MeasureReading {
    MeasureAction action = MeasureAction::Distance;
    bool active = false;  // поставлена хоть одна точка
    double value = 0;     // в единицах масштаба (площадь — в квадратных)
    double dx = 0;        // последний отрезок по x и y (y — вверх), единицы масштаба
    double dy = 0;
    double angle = 0;  // последнего отрезка, градусы против часовой стрелки
    measure::Scale scale;
    bool documentScale = false;  // масштаб — из /VP документа
};

class MeasureMode : public QObject, public ViewTool {
    Q_OBJECT
public:
    static constexpr double kSnapRadiusPx = 8;  // привязка к вершине (эталон: 8 px)
    static constexpr double kClosePx = 6;       // щелчок в первую вершину замыкает площадь
    static constexpr double kMinSegmentPx = 1;  // ближе — та же точка

    MeasureMode(PageView& view, ToolController& controller);
    ~MeasureMode() override;

    ViewToolId id() const noexcept override { return ViewToolId::Measure; }
    QString title() const override;
    void activate() override;
    void deactivate() override;
    bool mousePress(QMouseEvent* event) override;
    bool mouseMove(QMouseEvent* event) override;
    bool mouseRelease(QMouseEvent* event) override;
    bool mouseDoubleClick(QMouseEvent* event) override;
    bool keyPress(QKeyEvent* event) override;
    void paintOverlay(QPainter& painter) override;
    QCursor cursor(QPointF point, Qt::KeyboardModifiers modifiers) const override;
    bool cancel() override;
    bool escape(bool cancelled) override;
    void documentChanged() override;
    void pageChanged(int page) override;

    MeasureAction action() const noexcept { return action_; }
    void setAction(MeasureAction action);
    static QString actionTitle(MeasureAction action);
    static measure::Kind kindOf(MeasureAction action) noexcept;

    void setLocked(const QString& reason);
    const QString& lockReason() const noexcept { return lock_; }
    void setSettings(const measure::MeasureSettings& settings);
    const measure::MeasureSettings& settings() const noexcept { return settings_; }

    // Масштаб в точке страницы: документа (/VP, если включено и известно) или пользователя.
    measure::Scale scaleAt(int page, QPointF point, bool* fromDocument = nullptr);
    // Видовые экраны страницы (кеш; нет — запрос задачей, ответ — viewportsReady).
    const std::vector<measure::Viewport>* viewports(int page);
    // Привязки страницы (кеш; нет — запрос задачей, ответ — snapReady).
    const measure::SnapIndex* snapIndex(int page);
    // Точка после привязки и ограничений (Shift — 45°, «Ортогонально» — 90° от последней точки); snapped —
    // привязана к вершине.
    QPointF adjust(int page, QPointF raw, Qt::KeyboardModifiers modifiers, bool* snapped = nullptr);

    // Поставить точку (пункты страницы; как щелчок после adjust) — сценарии и тесты.
    void addPoint(int page, QPointF point);
    // Закончить измерение (Enter): мало точек — бросить.
    void finish();
    const std::vector<QPointF>& points() const noexcept { return points_; }
    int page() const noexcept { return page_; }
    // Курсор (пункты страницы page()) и привязан ли он.
    QPointF hover() const noexcept { return hover_; }
    bool hoverSnapped() const noexcept { return hoverSnapped_; }
    const MeasureReading& reading() const noexcept { return reading_; }

    // Окно «Сведения об измерении» (создаётся при первом показе; родитель — вид).
    MeasureInfoPanel* infoPanel() const;
    void setInfoVisible(bool visible);

    // Подписи: единица (кв. — «м²»), значение с единицей «3,53 м», масштаб «1 см = 10 м».
    static QString unitLabel(measure::Unit unit, bool area = false);
    static QString formatValue(double value, const measure::Scale& scale, bool area);
    static QString scaleText(const measure::Scale& scale);
    static QString number(double value, int precision);

signals:
    void created(const ot::annot::AnnotRef& ref);
    void failed(const QString& message);
    void actionChanged(ot::ui::MeasureAction action);
    void readingChanged();
    void snapReady(int page);
    void viewportsReady(int page);
    // Калибровка: отрезок lengthPt пунктов на странице page.
    void calibrationRequested(int page, double lengthPt);

private:
    DocumentSession* session() const;
    void updateReading();
    void updateHover(QPointF viewportPoint, Qt::KeyboardModifiers modifiers);
    void reset();
    std::size_t needed() const noexcept;

    MeasureAction action_ = MeasureAction::Distance;
    QString lock_;
    measure::MeasureSettings settings_;
    std::vector<QPointF> points_;  // пункты страницы page_
    int page_ = -1;
    QPointF hover_;
    int hoverPage_ = -1;
    bool hoverSnapped_ = false;
    bool hasHover_ = false;
    QPointF hoverViewport_;
    Qt::KeyboardModifiers modifiers_;
    MeasureReading reading_;
    std::map<int, measure::SnapIndex> snaps_;
    std::set<int> snapPending_;
    std::map<int, std::vector<measure::Viewport>> viewports_;
    std::set<int> viewportsPending_;
    quint64 generation_ = 0;
    bool pressed_ = false;
    QPointer<MeasureInfoPanel> info_;
};

}  // namespace ot::ui
