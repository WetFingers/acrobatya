#include "ui/tools/measure/measure_tool.h"

#include <QCoreApplication>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>

#include "services/document_session.h"
#include "services/measure_ops.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "ui/shell/document_view.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/measure/measure_info_pane.h"
#include "ui/tools/measure/measure_scale_dialog.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/toast.h"

namespace ot::ui {

namespace {

struct ActionRow {
    MeasureAction action;
    const char* name;  // objectName кнопки
    const char* icon;
};

constexpr ActionRow kActionRows[] = {
    {MeasureAction::Distance, "measureDistance", "ruler-dimension-line"},
    {MeasureAction::Perimeter, "measurePerimeter", "spline"},
    {MeasureAction::Area, "measureArea", "land-plot"},
};

}  // namespace

MeasureTool::MeasureTool(ToolRegistry& registry) : registry_(registry) {}

// Реестр — статический: после QApplication панель не удаляется (её окно уже закрыто).
MeasureTool::~MeasureTool() {
    if (QCoreApplication::instance() != nullptr) {
        delete panel_.data();
    }
}

QString MeasureTool::title() const { return QCoreApplication::translate("ot::ui::ToolRegistry", "Измерить"); }

QString MeasureTool::icon() const { return QLatin1String(toolIconName("measure")); }

MeasureMode& MeasureTool::modeOf(PageView& view) {
    ToolController& tools = view.tools();
    if (auto* mode = dynamic_cast<MeasureMode*>(tools.find(ViewToolId::Measure))) {
        return *mode;
    }
    return *static_cast<MeasureMode*>(tools.install(std::make_unique<MeasureMode>(view, tools)));
}

DocumentView* MeasureTool::view() const { return view_; }

MeasureMode* MeasureTool::mode() const {
    if (view_ == nullptr) {
        return nullptr;
    }
    return dynamic_cast<MeasureMode*>(view_->view().tools().find(ViewToolId::Measure));
}

void MeasureTool::activate(DocumentView& view) {
    for (const QMetaObject::Connection& c : connections_) {
        QObject::disconnect(c);
    }
    connections_.clear();
    view_ = &view;
    MeasureMode& m = modeOf(view.view());
    m.setSettings(measure::settings(Settings()));
    connections_.push_back(connect(&view, &DocumentView::readOnlyChanged, this, [this] { updateLock(); }));
    connections_.push_back(
        connect(&view, &DocumentView::protectedViewChanged, this, [this] { updateLock(); }));
    connections_.push_back(connect(&view.session(), &DocumentSession::saved, this, [this] { updateLock(); }));
    connections_.push_back(connect(&m, &MeasureMode::failed, this, [this](const QString& message) {
        if (view_ != nullptr) {
            Toast::notify(view_, tr("Не добавлено: %1").arg(message), Tone::Danger);
        }
    }));
    connections_.push_back(connect(&m, &MeasureMode::calibrationRequested, this,
                                   [this](int /*page*/, double length) { showScaleDialog(length); }));
    connections_.push_back(connect(&m, &MeasureMode::readingChanged, this, [this] { updateScaleLabel(); }));
    connections_.push_back(
        connect(&view.view().tools(), &ToolController::toolChanged, this, [this] { syncButtons(); }));
    connections_.push_back(connect(&m, &MeasureMode::actionChanged, this, [this] { syncButtons(); }));
    updateLock();
    updateScaleLabel();
    start(m.action() == MeasureAction::Calibrate ? MeasureAction::Distance : m.action());
}

void MeasureTool::deactivate() {
    for (const QMetaObject::Connection& c : connections_) {
        QObject::disconnect(c);
    }
    connections_.clear();
    if (QCoreApplication::instance() == nullptr) {
        return;  // реестр — статический: после QApplication виджетов уже не трогать
    }
    if (view_ != nullptr) {
        ToolController& tools = view_->view().tools();
        if (tools.tool() == ViewToolId::Measure) {
            tools.setTool(ToolController::defaultTool());
        }
    }
    view_ = nullptr;
    syncButtons();
}

void MeasureTool::settingsChanged() { reloadSettings(); }

void MeasureTool::reloadSettings() {
    const measure::MeasureSettings s = measure::settings(Settings());
    if (MeasureMode* m = mode()) {
        m->setSettings(s);
        if (view_ != nullptr && view_->view().tools().tool() == ViewToolId::Measure) {
            m->setInfoVisible(s.showInfo);
        }
    }
    if (panel_ != nullptr) {
        const QSignalBlocker b1(snap_);
        const QSignalBlocker b2(ortho_);
        const QSignalBlocker b3(info_);
        snap_->setChecked(s.snap);
        ortho_->setChecked(s.ortho);
        info_->setChecked(s.showInfo);
    }
    updateScaleLabel();
}

void MeasureTool::saveFlags() {
    Settings settings;
    settings.set(settings::kMeasureSnap, snap_->isChecked());
    settings.set(settings::kMeasureOrtho, ortho_->isChecked());
    settings.set(settings::kMeasureShowInfo, info_->isChecked());
    reloadSettings();
}

void MeasureTool::start(MeasureAction action) {
    if (view_ == nullptr || !lock_.isEmpty()) {
        return;
    }
    PageView& pv = view_->view();
    MeasureMode& m = modeOf(pv);
    m.setAction(action);
    pv.tools().setTool(ViewToolId::Measure);
    pv.setFocus();
    syncButtons();
}

void MeasureTool::updateLock() {
    QString reason;
    if (view_ != nullptr) {
        const DocumentView& v = *view_;
        if (v.isViewOnly()) {
            reason = tr("Измерять можно только в PDF: сохраните файл как PDF.");
        } else if (v.isProtectedView()) {
            reason = tr("Защищённый просмотр: измерения отключены. Нажмите «Включить все функции».");
        } else if (v.isReadOnly()) {
            reason = tr("Документ открыт только для чтения: нажмите «Включить редактирование».");
        } else if (!v.permissions().annotate) {
            reason = tr("Документ защищён: добавлять комментарии и измерения запрещено.");
        }
    }
    if (MeasureMode* m = mode()) {
        m->setLocked(reason);
        if (!reason.isEmpty() && view_->view().tools().tool() == ViewToolId::Measure) {
            view_->view().tools().setTool(ToolController::defaultTool());
        }
    }
    if (reason != lock_) {
        lock_ = reason;
        emit lockChanged(reason);
    }
    syncButtons();
}

void MeasureTool::syncButtons() {
    if (panel_ == nullptr) {
        return;
    }
    const bool usable = view_ != nullptr && lock_.isEmpty();
    MeasureMode* m = mode();
    const bool active = usable && m != nullptr && view_->view().tools().tool() == ViewToolId::Measure;
    for (const auto& [action, button] : buttons_) {
        if (button == nullptr) {
            continue;
        }
        button->setEnabled(usable);
        button->setChecked(active && m->action() == action);
        button->setToolTip(usable ? MeasureMode::actionTitle(action) : lock_);
    }
    if (lockLabel_ != nullptr) {
        lockLabel_->setText(lock_);
        lockLabel_->setVisible(!lock_.isEmpty());
    }
}

void MeasureTool::updateScaleLabel() {
    if (scaleLabel_ == nullptr) {
        return;
    }
    MeasureMode* m = mode();
    const measure::Scale scale = m != nullptr ? m->reading().scale : measure::settings(Settings()).scale;
    const bool document = m != nullptr && m->reading().documentScale;
    const QString text = MeasureMode::scaleText(scale);
    const QString precision = MeasureMode::number(std::pow(10.0, -scale.precision), scale.precision);
    scaleLabel_->setText(document ? tr("%1, точность %2 — из документа").arg(text, precision)
                                  : tr("%1, точность %2").arg(text, precision));
}

ToolButton* MeasureTool::actionButton(MeasureAction action) const {
    const auto it = buttons_.find(action);
    return it == buttons_.end() ? nullptr : it->second.data();
}

MeasureScaleDialog* MeasureTool::showScaleDialog(double calibrationPt) {
    measure::Scale current = measure::settings(Settings()).scale;
    std::optional<measure::Scale> document;
    if (MeasureMode* m = mode(); m != nullptr && m->reading().documentScale) {
        document = m->reading().scale;
    }
    QWidget* parent = view_ != nullptr ? view_->window() : panel_.data();
    auto* dialog = MeasureScaleDialog::openFor(current, parent);
    dialog->setDocumentScale(document);
    if (calibrationPt > 0) {
        dialog->setCalibration(calibrationPt);
    }
    connect(dialog, &MeasureScaleDialog::scaleChosen, this, [this](const measure::Scale& scale) {
        Settings settings;
        measure::setScale(settings, scale);
        reloadSettings();
    });
    return dialog;
}

// ---------------------------------------------------------------- панель

void MeasureTool::addCaption(QVBoxLayout* column, const QString& text) {
    auto* caption = new QLabel(text, panel_);
    caption->setObjectName(QStringLiteral("measureSectionCaption"));
    caption->setProperty("caption", true);
    column->addSpacing(Theme::sizes().grid * 2);
    column->addWidget(caption);
}

ToolButton* MeasureTool::addRow(QVBoxLayout* column, const QString& icon, const QString& label) {
    auto* b = new ToolButton(icon, label, panel_);
    b->setLabelVisible(true);
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    column->addWidget(b);
    return b;
}

QWidget* MeasureTool::panel() {
    if (panel_ == nullptr) {
        buildPanel();
        reloadSettings();
        syncButtons();
    }
    return panel_;
}

void MeasureTool::buildPanel() {
    const Metrics& m = Theme::sizes();
    auto* scroll = new QScrollArea();
    scroll->setObjectName(QStringLiteral("measureToolPanel"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    panel_ = scroll;
    auto* body = new QWidget(scroll);
    body->setObjectName(QStringLiteral("measureToolPanelBody"));
    body->setAutoFillBackground(false);
    auto* column = new QVBoxLayout(body);
    column->setContentsMargins(m.grid * 3, m.grid * 3, m.grid * 3, m.grid * 3);
    column->setSpacing(m.grid);

    lockLabel_ = new QLabel(panel_);
    lockLabel_->setObjectName(QStringLiteral("measureLockNote"));
    lockLabel_->setProperty("note", QStringLiteral("warn"));
    lockLabel_->setWordWrap(true);
    lockLabel_->setVisible(false);
    column->addWidget(lockLabel_);

    addCaption(column, tr("Измерить"));
    for (const ActionRow& row : kActionRows) {
        ToolButton* b = addRow(column, QLatin1String(row.icon), MeasureMode::actionTitle(row.action));
        b->setObjectName(QLatin1String(row.name));
        b->setCheckable(true);
        const MeasureAction action = row.action;
        connect(b, &ToolButton::clicked, this, [this, action] { start(action); });
        buttons_[action] = b;
    }
    auto* hint =
        new QLabel(tr("Щёлкайте по точкам страницы. Конец периметра и площади — двойной щелчок, Enter "
                      "или правая кнопка; Shift — под углом 45°."),
                   panel_);
    hint->setObjectName(QStringLiteral("measureHint"));
    hint->setWordWrap(true);
    column->addWidget(hint);

    addCaption(column, tr("Масштаб"));
    scaleLabel_ = new QLabel(panel_);
    scaleLabel_->setObjectName(QStringLiteral("measureScaleText"));
    scaleLabel_->setWordWrap(true);
    column->addWidget(scaleLabel_);
    scaleButton_ = addRow(column, QStringLiteral("scaling"), tr("Масштаб и точность…"));
    scaleButton_->setObjectName(QStringLiteral("measureScaleButton"));
    connect(scaleButton_, &ToolButton::clicked, this, [this] { showScaleDialog(); });
    ToolButton* calibrate = addRow(column, QStringLiteral("ruler"), tr("Калибровать по отрезку"));
    calibrate->setObjectName(QStringLiteral("measureCalibrate"));
    calibrate->setCheckable(true);
    calibrate->setToolTip(tr("Проведите отрезок известной длины и введите его настоящую длину"));
    connect(calibrate, &ToolButton::clicked, this, [this] { start(MeasureAction::Calibrate); });
    buttons_[MeasureAction::Calibrate] = calibrate;

    addCaption(column, tr("Привязка и вид"));
    snap_ = new QCheckBox(tr("К &вершинам и концам линий"), panel_);
    snap_->setObjectName(QStringLiteral("measureSnap"));
    ortho_ = new QCheckBox(tr("&Ортогонально (по горизонтали и вертикали)"), panel_);
    ortho_->setObjectName(QStringLiteral("measureOrtho"));
    info_ = new QCheckBox(tr("&Сведения об измерении"), panel_);
    info_->setObjectName(QStringLiteral("measureShowInfo"));
    for (QCheckBox* box : {snap_.data(), ortho_.data(), info_.data()}) {
        column->addWidget(box);
        connect(box, &QCheckBox::toggled, this, [this] { saveFlags(); });
    }
    column->addStretch(1);
    scroll->setWidget(body);
    updateScaleLabel();
}

}  // namespace ot::ui
