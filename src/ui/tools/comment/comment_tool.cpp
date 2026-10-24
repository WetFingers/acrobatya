#include "ui/tools/comment/comment_tool.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>
#include <iterator>

#include <memory>

#include "core/annot/defaults.h"
#include "services/comment_settings.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "ui/shell/document_view.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/color_swatch.h"
#include "ui/widgets/number_field.h"
#include "ui/widgets/toast.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

namespace {

struct ActionRow {
    CommentAction action;
    const char* name;  // objectName кнопки
    const char* icon;
};

constexpr ActionRow kTextRows[] = {
    {CommentAction::Note, "commentNote", "sticky-note"},
    {CommentAction::Highlight, "commentHighlight", "highlighter"},
    {CommentAction::Underline, "commentUnderline", "underline"},
    {CommentAction::StrikeOut, "commentStrikeOut", "strikethrough"},
    {CommentAction::Squiggly, "commentSquiggly", "spline"},
};

constexpr ActionRow kEditRows[] = {
    {CommentAction::InsertText, "commentInsertText", "text-cursor"},
    {CommentAction::ReplaceText, "commentReplaceText", "replace"},
};

constexpr ActionRow kDrawRows[] = {
    {CommentAction::Ink, "commentInk", "pencil"},
    {CommentAction::Eraser, "commentEraser", "eraser"},
};

}  // namespace

CommentTool::CommentTool(ToolRegistry& registry) : registry_(registry) {}

// Реестр — статический: после QApplication панель не удаляется (её окно уже закрыто).
CommentTool::~CommentTool() {
    if (QCoreApplication::instance() != nullptr) {
        delete panel_.data();
    }
}

QString CommentTool::title() const {
    return QCoreApplication::translate("ot::ui::ToolRegistry", "Добавить комментарии");
}

QString CommentTool::icon() const { return QLatin1String(toolIconName("comment")); }

CommentMode& CommentTool::modeOf(PageView& view) {
    ToolController& tools = view.tools();
    if (auto* mode = dynamic_cast<CommentMode*>(tools.find(ViewToolId::Comment))) {
        return *mode;
    }
    return *static_cast<CommentMode*>(tools.install(std::make_unique<CommentMode>(view, tools)));
}

CommentMode* CommentTool::mode() const {
    if (view_ == nullptr) {
        return nullptr;
    }
    return dynamic_cast<CommentMode*>(view_->view().tools().find(ViewToolId::Comment));
}

void CommentTool::activate(DocumentView& view) {
    for (const QMetaObject::Connection& c : connections_) {
        QObject::disconnect(c);
    }
    connections_.clear();
    view_ = &view;
    CommentMode& m = modeOf(view.view());
    connections_.push_back(connect(&view, &DocumentView::readOnlyChanged, this, [this] { updateLock(); }));
    connections_.push_back(
        connect(&view, &DocumentView::protectedViewChanged, this, [this] { updateLock(); }));
    connections_.push_back(connect(&view.session(), &DocumentSession::saved, this, [this] { updateLock(); }));
    connections_.push_back(connect(&m, &CommentMode::failed, this, [this](const QString& message) {
        if (view_ != nullptr) {
            Toast::notify(view_, tr("Комментарий не добавлен: %1").arg(message), Tone::Danger);
        }
    }));
    connections_.push_back(
        connect(&view.view().tools(), &ToolController::toolChanged, this, [this] { syncButtons(); }));
    connections_.push_back(connect(&m, &CommentMode::actionChanged, this, [this] {
        syncButtons();
        loadStyle();
    }));
    settingsChanged();
    updateLock();
}

void CommentTool::deactivate() {
    for (const QMetaObject::Connection& c : connections_) {
        QObject::disconnect(c);
    }
    connections_.clear();
    if (QCoreApplication::instance() == nullptr) {
        return;  // реестр — статический: после QApplication виджетов уже не трогать
    }
    if (view_ != nullptr) {
        ToolController& tools = view_->view().tools();
        if (tools.tool() == ViewToolId::Comment) {
            tools.setTool(ToolController::defaultTool());
        }
    }
    view_ = nullptr;
    syncButtons();
}

void CommentTool::settingsChanged() {
    setPinned(Settings().get(settings::kCommentKeepTool));
    loadStyle();
}

void CommentTool::setPinned(bool pinned) {
    if (view_ != nullptr) {
        view_->view().tools().setPinned(pinned);
    }
    if (pin_ != nullptr && pin_->isChecked() != pinned) {
        const QSignalBlocker block(pin_);
        pin_->setChecked(pinned);
    }
}

// Стиль действия в поля панели (умолчания ядра + свой стиль из settings.ini).
void CommentTool::loadStyle() {
    if (panel_ == nullptr) {
        return;
    }
    const CommentAction action = mode() != nullptr ? mode()->action() : CommentAction::Note;
    const annot::AnnotProps p =
        comments::newProps(Settings(), CommentMode::typeOf(action), CommentMode::actionKey(action));
    loadingStyle_ = true;
    color_->setColor(p.color.empty() ? QColor() : QColor::fromRgb(static_cast<QRgb>(p.color.toRgb())));
    width_->setValue(p.borderWidth);
    width_->setEnabled(action == CommentAction::Ink);
    opacity_->setValue(std::round(p.opacity * 100));
    const int icon = icon_->findData(QString::fromStdString(p.icon));
    icon_->setCurrentIndex(icon >= 0 ? icon : 0);
    icon_->setEnabled(action == CommentAction::Note);
    loadingStyle_ = false;
}

void CommentTool::saveStyle() {
    if (loadingStyle_ || panel_ == nullptr) {
        return;
    }
    const CommentAction action = mode() != nullptr ? mode()->action() : CommentAction::Note;
    comments::ToolStyle style;
    if (color_->color().isValid()) {
        style.color =
            annot::AnnotColor::fromRgb(static_cast<std::uint32_t>(color_->color().rgb() & 0xffffff));
    }
    style.opacity = static_cast<float>(opacity_->value() / 100.0);
    if (action == CommentAction::Ink) {
        style.width = static_cast<float>(width_->value());
    }
    if (action == CommentAction::Note) {
        style.icon = icon_->currentData().toString().toStdString();
    }
    Settings settings;
    comments::setToolStyle(settings, CommentMode::actionKey(action), style);
}

void CommentTool::start(CommentAction action) {
    if (view_ == nullptr || !lock_.isEmpty()) {
        return;
    }
    PageView& pv = view_->view();
    CommentMode& m = modeOf(pv);
    m.setAction(action);
    pv.tools().setTool(ViewToolId::Comment);
    pv.setFocus();
    syncButtons();
}

void CommentTool::updateLock() {
    QString reason;
    if (view_ != nullptr) {
        const DocumentView& v = *view_;
        if (v.isViewOnly()) {
            reason = tr("Комментировать можно только PDF: сохраните файл как PDF.");
        } else if (v.isProtectedView()) {
            reason = tr("Защищённый просмотр: комментарии отключены. Нажмите «Включить все функции».");
        } else if (v.isReadOnly()) {
            reason = tr("Документ открыт только для чтения: нажмите «Включить редактирование».");
        } else if (!v.permissions().annotate) {
            reason = tr("Документ защищён: добавление комментариев запрещено.");
        }
    }
    if (CommentMode* m = mode()) {
        m->setLocked(reason);
        if (!reason.isEmpty() && view_->view().tools().tool() == ViewToolId::Comment) {
            view_->view().tools().setTool(ToolController::defaultTool());
        }
    }
    if (reason != lock_) {
        lock_ = reason;
        emit lockChanged(reason);
    }
    syncButtons();
}

void CommentTool::syncButtons() {
    if (panel_ == nullptr) {
        return;
    }
    const bool usable = view_ != nullptr && lock_.isEmpty();
    CommentMode* m = mode();
    const bool active = usable && m != nullptr && view_->view().tools().tool() == ViewToolId::Comment;
    for (const auto& [action, button] : buttons_) {
        if (button == nullptr) {
            continue;
        }
        button->setEnabled(usable);
        button->setChecked(active && m->action() == action);
        button->setToolTip(usable ? CommentMode::actionTitle(action) : lock_);
    }
    if (lockLabel_ != nullptr) {
        lockLabel_->setText(lock_);
        lockLabel_->setVisible(!lock_.isEmpty());
    }
}

ToolButton* CommentTool::actionButton(CommentAction action) const {
    const auto it = buttons_.find(action);
    return it == buttons_.end() ? nullptr : it->second.data();
}

// ---------------------------------------------------------------- панель

void CommentTool::addCaption(QVBoxLayout* column, const QString& text) {
    auto* caption = new QLabel(text, panel_);
    caption->setObjectName(QStringLiteral("commentSectionCaption"));
    column->addSpacing(Theme::sizes().grid * 2);
    column->addWidget(caption);
}

ToolButton* CommentTool::addRow(QVBoxLayout* column, const QString& icon, const QString& label) {
    auto* b = new ToolButton(icon, label, panel_);
    b->setLabelVisible(true);
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    column->addWidget(b);
    return b;
}

QWidget* CommentTool::panel() {
    if (panel_ == nullptr) {
        buildPanel();
        syncButtons();
    }
    return panel_;
}

void CommentTool::buildPanel() {
    const Metrics& m = Theme::sizes();
    panel_ = new QWidget();
    panel_->setObjectName(QStringLiteral("commentToolPanel"));
    auto* column = new QVBoxLayout(panel_);
    column->setContentsMargins(m.grid * 3, m.grid * 3, m.grid * 3, m.grid * 3);
    column->setSpacing(m.grid);

    lockLabel_ = new QLabel(panel_);
    lockLabel_->setObjectName(QStringLiteral("commentLockNote"));
    lockLabel_->setWordWrap(true);
    lockLabel_->setVisible(false);
    column->addWidget(lockLabel_);

    auto addAction = [this, column](const ActionRow& row) {
        ToolButton* b = addRow(column, QLatin1String(row.icon), CommentMode::actionTitle(row.action));
        b->setObjectName(QLatin1String(row.name));
        b->setCheckable(true);
        const CommentAction action = row.action;
        connect(b, &ToolButton::clicked, this, [this, action] { start(action); });
        buttons_[action] = b;
    };
    addCaption(column, tr("Комментарии"));
    for (const ActionRow& row : kTextRows) {
        addAction(row);
    }
    addCaption(column, tr("Правка текста"));
    for (const ActionRow& row : kEditRows) {
        addAction(row);
    }
    addCaption(column, tr("Рисование"));
    for (const ActionRow& row : kDrawRows) {
        addAction(row);
    }
    buildStyle(column);
    column->addStretch(1);
}

void CommentTool::buildStyle(QVBoxLayout* column) {
    addCaption(column, tr("Стиль"));
    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(Theme::sizes().grid);
    color_ = new ColorSwatch(QColor(), panel_);
    color_->setObjectName(QStringLiteral("commentColor"));
    color_->setAccessibleName(tr("Цвет"));
    form->addRow(tr("Цвет:"), color_);
    width_ = new NumberField(panel_);
    width_->setObjectName(QStringLiteral("commentWidth"));
    width_->setRange(0.5, 12);
    width_->setSingleStep(0.5);
    width_->setDecimals(1);
    width_->setUnit(tr("пт"));
    form->addRow(tr("Толщина:"), width_);
    opacity_ = new NumberField(panel_);
    opacity_->setObjectName(QStringLiteral("commentOpacity"));
    opacity_->setRange(5, 100);
    opacity_->setSingleStep(5);
    opacity_->setDecimals(0);
    opacity_->setUnit(QStringLiteral("%"));
    form->addRow(tr("Непрозрачность:"), opacity_);
    icon_ = new QComboBox(panel_);
    icon_->setObjectName(QStringLiteral("commentNoteIcon"));
    static const char* const kIconTitles[] = {QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Комментарий"),
                                              QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Ключ"),
                                              QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Записка"),
                                              QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Справка"),
                                              QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Новый абзац"),
                                              QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Абзац"),
                                              QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Вставка")};
    for (std::size_t i = 0; i < std::size(annot::kNoteIcons); ++i) {
        icon_->addItem(tr(kIconTitles[i]), QLatin1String(annot::kNoteIcons[i]));
    }
    form->addRow(tr("Значок записки:"), icon_);
    column->addLayout(form);
    pin_ = addRow(column, QStringLiteral("pin"), tr("Оставлять инструмент выбранным"));
    pin_->setObjectName(QStringLiteral("commentPin"));
    pin_->setCheckable(true);
    pin_->setToolTip(tr("После записки, вставки или замены текста инструмент не сменяется на «Выделение»"));
    connect(pin_, &ToolButton::toggled, this, [this](bool on) {
        Settings().set(settings::kCommentKeepTool, on);
        setPinned(on);
    });
    connect(color_, &ColorSwatch::colorChanged, this, [this] { saveStyle(); });
    connect(width_, &NumberField::valueChanged, this, [this] { saveStyle(); });
    connect(opacity_, &NumberField::valueChanged, this, [this] { saveStyle(); });
    connect(icon_, &QComboBox::currentIndexChanged, this, [this] { saveStyle(); });
    pin_->setChecked(Settings().get(settings::kCommentKeepTool));
    loadStyle();
}

// ---------------------------------------------------------------- команды

std::vector<ot::Command> CommentTool::commands() const {
    std::vector<ot::Command> out;
    ToolRegistry* registry = &registry_;
    const CommentTool* self = this;
    auto runner = [registry, self](CommentAction action) {
        return [registry, self, action] {
            if (DocumentView* v = registry->view()) {
                registry->activate(QStringLiteral("comment"), v);
                const_cast<CommentTool*>(self)->start(action);
            }
        };
    };
    auto enabled = [registry] { return registry->view() != nullptr && registry->view()->canAnnotate(); };
    out.push_back({.id = QStringLiteral("tool.note"),
                   .title = tr("&Записка"),
                   .shortcuts = {QKeySequence(Qt::Key_S)},
                   .icon = QStringLiteral("sticky-note"),
                   .run = runner(CommentAction::Note),
                   .when = QLatin1String(CommandRegistry::kViewContext),
                   .enabled = enabled,
                   .menu = QStringLiteral("tools"),
                   .singleKey = true});
    out.push_back({.id = QStringLiteral("comment.note"),
                   .title = tr("Добавить &записку"),
                   .shortcuts = {QKeySequence(Qt::CTRL | Qt::Key_6)},
                   .icon = QStringLiteral("sticky-note"),
                   .run = runner(CommentAction::Note),
                   .enabled = enabled,
                   .menu = QStringLiteral("tools")});
    out.push_back({.id = QStringLiteral("tool.draw"),
                   .title = tr("&Рисовать"),
                   .shortcuts = {QKeySequence(Qt::Key_D)},
                   .icon = QStringLiteral("pencil"),
                   .run = runner(CommentAction::Ink),
                   .when = QLatin1String(CommandRegistry::kViewContext),
                   .enabled = enabled,
                   .menu = QStringLiteral("tools"),
                   .singleKey = true});
    out.push_back({.id = QStringLiteral("tool.highlight"),
                   .title = tr("&Выделить текст"),
                   .shortcuts = {QKeySequence(Qt::Key_U)},
                   .icon = QStringLiteral("highlighter"),
                   .run = runner(CommentAction::Highlight),
                   .when = QLatin1String(CommandRegistry::kViewContext),
                   .enabled = enabled,
                   .menu = QStringLiteral("tools"),
                   .singleKey = true});
    return out;
}

}  // namespace ot::ui
