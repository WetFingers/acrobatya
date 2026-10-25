#include "services/measure_ops.h"

#include <cmath>

#include "services/settings.h"
#include "services/settings_keys.h"
#include "services/task_context.h"

namespace ot::measure {

QFuture<std::vector<PointF>> snapPoints(DocumentSession& session, int page, TaskPriority priority) {
    return session.post([page](TaskContext& tc) { return measure::snapPoints(tc.document(), page); },
                        priority, QStringLiteral("Measure snap points"));
}

QFuture<std::vector<Viewport>> viewports(DocumentSession& session, int page, TaskPriority priority) {
    return session.post([page](TaskContext& tc) { return measure::viewports(tc.document(), page); }, priority,
                        QStringLiteral("Measure viewports"));
}

QFuture<annot::AnnotRef> create(DocumentSession& session, int page, MeasureProps props) {
    const std::string operation = createStepName(props.kind);
    return session.edit(QString::fromStdString(operation),
                        [page, props = std::move(props), operation](TaskContext& tc) {
                            annot::AnnotRef ref = measure::create(tc.document(), page, props, operation);
                            tc.touchPage(page);
                            return ref;
                        });
}

QFuture<std::vector<Measurement>> list(DocumentSession& session, TaskPriority priority) {
    return session.post([](TaskContext& tc) { return listMeasurements(tc.document()); }, priority,
                        QStringLiteral("Measurements"));
}

MeasureSettings settings(const Settings& s) {
    MeasureSettings out;
    const int precision = s.get(settings::kMeasurePrecision);
    std::optional<Scale> scale = parseRatio(s.get(settings::kMeasureScale).toStdString(), precision);
    if (!scale) {
        scale = parseRatio(settings::kMeasureScale.def, precision);
    }
    out.scale = scale.value_or(Scale{});
    out.useDocumentScale = s.get(settings::kMeasureUseDocumentScale);
    out.snap = s.get(settings::kMeasureSnap);
    out.ortho = s.get(settings::kMeasureOrtho);
    const QString ends = s.get(settings::kMeasureLineEnds);
    out.lineEnds = ends == QLatin1String("tick")   ? LineEnds::Tick
                   : ends == QLatin1String("none") ? LineEnds::None
                                                   : LineEnds::Arrow;
    out.leaderLength = static_cast<float>(s.get(settings::kMeasureLeaderLength));
    out.leaderExtension = static_cast<float>(s.get(settings::kMeasureLeaderExtension));
    out.leaderOffset = static_cast<float>(s.get(settings::kMeasureLeaderOffset));
    out.captionTop = s.get(settings::kMeasureCaption) == QLatin1String("top");
    out.showInfo = s.get(settings::kMeasureShowInfo);
    return out;
}

void setScale(Settings& s, const Scale& scale) {
    s.set(settings::kMeasureScale, QString::fromStdString(scale.ratio()));
    s.set(settings::kMeasurePrecision, scale.precision);
}

void setSettings(Settings& s, const MeasureSettings& value) {
    setScale(s, value.scale);
    s.set(settings::kMeasureUseDocumentScale, value.useDocumentScale);
    s.set(settings::kMeasureSnap, value.snap);
    s.set(settings::kMeasureOrtho, value.ortho);
    s.set(settings::kMeasureLineEnds, value.lineEnds == LineEnds::Tick   ? QStringLiteral("tick")
                                      : value.lineEnds == LineEnds::None ? QStringLiteral("none")
                                                                         : QStringLiteral("arrow"));
    s.set(settings::kMeasureLeaderLength, static_cast<int>(std::lround(value.leaderLength)));
    s.set(settings::kMeasureLeaderExtension, static_cast<int>(std::lround(value.leaderExtension)));
    s.set(settings::kMeasureLeaderOffset, static_cast<int>(std::lround(value.leaderOffset)));
    s.set(settings::kMeasureCaption, value.captionTop ? QStringLiteral("top") : QStringLiteral("inline"));
    s.set(settings::kMeasureShowInfo, value.showInfo);
}

annot::LineEnding lineEnding(LineEnds ends) noexcept {
    switch (ends) {
        case LineEnds::Arrow:
            return annot::LineEnding::OpenArrow;
        case LineEnds::Tick:
            return annot::LineEnding::Butt;
        case LineEnds::None:
            return annot::LineEnding::None;
    }
    return annot::LineEnding::None;
}

}  // namespace ot::measure
