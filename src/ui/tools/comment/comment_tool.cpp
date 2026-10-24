#include "ui/tools/comment/comment_tool.h"

#include <QCoreApplication>
#include <QVBoxLayout>

#include <memory>

#include "services/document_session.h"
#include "ui/shell/document_view.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
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
    connections_.push_back(connect(&m, &CommentMode::actionChanged, this, [this] { syncButtons(); }));
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

void CommentTool::settingsChanged() {}

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
    column->addStretch(1);
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
