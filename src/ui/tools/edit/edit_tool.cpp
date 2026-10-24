#include "ui/tools/edit/edit_tool.h"

#include <QCoreApplication>
#include <QLabel>
#include <QVBoxLayout>

#include <memory>

#include "services/edit_ops.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/edit/edit_mode.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/toast.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

namespace {

// Пункты, которые сделают следующие этапы (эталон 6.2 п. 1): иконка, подпись, этап.
struct LaterItem {
    const char* icon;
    const char* label;
    int stage;
};
constexpr LaterItem kLaterItems[] = {
    {"file-text", QT_TRANSLATE_NOOP("ot::ui::EditTool", "Колонтитулы"), 7},
    {"stamp", QT_TRANSLATE_NOOP("ot::ui::EditTool", "Водяной знак"), 7},
    {"palette", QT_TRANSLATE_NOOP("ot::ui::EditTool", "Фон"), 7},
    {"link", QT_TRANSLATE_NOOP("ot::ui::EditTool", "Ссылка"), 7},
    {"paperclip", QT_TRANSLATE_NOOP("ot::ui::EditTool", "Вложить файл"), 7},
};

}  // namespace

EditTool::EditTool(ToolRegistry& registry) : registry_(registry) {}

// Реестр — статический: после QApplication панель не удаляется (её окно уже закрыто).
EditTool::~EditTool() {
    if (QCoreApplication::instance() != nullptr) {
        delete panel_.data();
    }
}

QString EditTool::title() const {
    return QCoreApplication::translate("ot::ui::ToolRegistry", "Редактировать PDF");
}

QString EditTool::icon() const { return QLatin1String(toolIconName("edit")); }

EditMode& EditTool::modeOf(PageView& view) {
    ToolController& tools = view.tools();
    if (auto* mode = dynamic_cast<EditMode*>(tools.find(ViewToolId::Edit))) {
        return *mode;
    }
    return *static_cast<EditMode*>(tools.install(std::make_unique<EditMode>(view, tools)));
}

EditMode* EditTool::mode() const {
    if (view_ == nullptr) {
        return nullptr;
    }
    return dynamic_cast<EditMode*>(view_->view().tools().find(ViewToolId::Edit));
}

void EditTool::activate(DocumentView& view) {
    for (const QMetaObject::Connection& c : connections_) {
        QObject::disconnect(c);
    }
    connections_.clear();
    view_ = &view;
    EditMode& mode = modeOf(view.view());
    connections_.push_back(connect(&view, &DocumentView::readOnlyChanged, this, [this] { updateLock(); }));
    connections_.push_back(
        connect(&view, &DocumentView::protectedViewChanged, this, [this] { updateLock(); }));
    connections_.push_back(connect(&view.session(), &DocumentSession::saved, this, [this] { updateLock(); }));
    connections_.push_back(connect(&mode, &EditMode::editFailed, this, [this](const QString& message) {
        if (view_ != nullptr) {
            Toast::notify(view_, message, Tone::Danger);
        }
    }));
    connections_.push_back(
        connect(&view.view().tools(), &ToolController::toolChanged, this, [this] { syncButtons(); }));
    docLock_.clear();
    updateLock();
    editText();
}

void EditTool::deactivate() {
    for (const QMetaObject::Connection& c : connections_) {
        QObject::disconnect(c);
    }
    connections_.clear();
    if (view_ != nullptr) {
        ToolController& tools = view_->view().tools();
        if (tools.tool() == ViewToolId::Edit) {
            tools.setTool(ToolController::defaultTool());
        }
    }
    view_ = nullptr;
    syncButtons();
}

void EditTool::editText() {
    if (view_ == nullptr) {
        return;
    }
    PageView& pv = view_->view();
    modeOf(pv);
    pv.tools().setTool(ViewToolId::Edit);
    pv.setFocus();
    syncButtons();
}

// Причина запрета: свойства вкладки сразу, подпись и /Perms — задачей документа.
void EditTool::updateLock() {
    if (view_ == nullptr) {
        return;
    }
    DocumentView& v = *view_;
    const quint64 request = ++lockRequest_;
    editing::lock(v.session()).then(this, [this, request](QFuture<edit::EditLock> done) {
        if (request != lockRequest_) {
            return;
        }
        edit::EditLock lock = edit::EditLock::None;
        try {
            lock = done.result();
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет ответа — не запрещать
        }
        switch (lock) {
            case edit::EditLock::Signed:
                docLock_ =
                    tr("Документ подписан: правка содержимого сломает подпись. Правка текста и "
                       "изображений отключена.");
                break;
            case edit::EditLock::Perms:
                docLock_ =
                    tr("Документ ограничивает изменения (права /Perms): правка текста и изображений "
                       "отключена.");
                break;
            case edit::EditLock::None:
                docLock_.clear();
                break;
        }
        applyLock();
    });
    applyLock();
}

void EditTool::applyLock() {
    QString reason;
    if (view_ != nullptr) {
        const DocumentView& v = *view_;
        if (v.isViewOnly()) {
            reason = tr("Править можно только PDF: сохраните файл как PDF.");
        } else if (v.isProtectedView()) {
            reason = tr("Защищённый просмотр: правка отключена. Нажмите «Включить все функции».");
        } else if (v.isReadOnly()) {
            reason = tr("Документ открыт только для чтения: нажмите «Включить редактирование».");
        } else if (!v.permissions().allows(Perm::Edit)) {
            reason = tr("Документ защищён: изменение содержимого запрещено.");
        } else {
            reason = docLock_;
        }
    }
    if (EditMode* m = mode()) {
        m->setLocked(reason);
    }
    if (reason != lock_) {
        lock_ = reason;
        emit lockChanged(reason);
    }
    syncButtons();
}

void EditTool::syncButtons() {
    if (panel_ == nullptr) {
        return;
    }
    const bool has = view_ != nullptr;
    const bool editable = has && lock_.isEmpty();
    if (text_ != nullptr) {
        text_->setEnabled(editable);
        text_->setChecked(editable && view_->view().tools().tool() == ViewToolId::Edit);
        text_->setToolTip(editable ? tr("Щёлкните в абзаце, чтобы править текст (E)") : lock_);
    }
    if (lockLabel_ != nullptr) {
        lockLabel_->setText(lock_);
        lockLabel_->setVisible(!lock_.isEmpty());
    }
}

// ---------------------------------------------------------------- панель

void EditTool::addCaption(QVBoxLayout* column, const QString& text) {
    auto* caption = new QLabel(text, panel_);
    caption->setObjectName(QStringLiteral("editSectionCaption"));
    column->addSpacing(Theme::sizes().grid * 2);
    column->addWidget(caption);
}

ToolButton* EditTool::addRow(QVBoxLayout* column, const QString& icon, const QString& label) {
    auto* b = new ToolButton(icon, label, panel_);
    b->setLabelVisible(true);
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    column->addWidget(b);
    return b;
}

QWidget* EditTool::panel() {
    if (panel_ == nullptr) {
        buildPanel();
        syncButtons();
    }
    return panel_;
}

void EditTool::buildPanel() {
    const Metrics& m = Theme::sizes();
    panel_ = new QWidget();
    panel_->setObjectName(QStringLiteral("editToolPanel"));
    auto* column = new QVBoxLayout(panel_);
    column->setContentsMargins(m.grid * 3, m.grid * 3, m.grid * 3, m.grid * 3);
    column->setSpacing(m.grid);

    lockLabel_ = new QLabel(panel_);
    lockLabel_->setObjectName(QStringLiteral("editLockNote"));
    lockLabel_->setWordWrap(true);
    lockLabel_->setVisible(false);
    column->addWidget(lockLabel_);

    text_ = addRow(column, QStringLiteral("text-cursor-input"), tr("Текст"));
    text_->setObjectName(QStringLiteral("editText"));
    text_->setCheckable(true);
    connect(text_, &ToolButton::clicked, this, [this] { editText(); });

    addCaption(column, tr("Ещё"));
    for (const LaterItem& item : kLaterItems) {
        ToolButton* b = addRow(column, QLatin1String(item.icon), tr(item.label));
        b->setEnabled(false);
        b->setToolTip(tr("«%1» появится в версии 0.%2").arg(tr(item.label)).arg(item.stage));
    }
    column->addStretch(1);
}

// ---------------------------------------------------------------- команды

std::vector<ot::Command> EditTool::commands() const {
    std::vector<ot::Command> out;
    ToolRegistry* registry = &registry_;
    out.push_back(
        {.id = QStringLiteral("tool.editText"),
         .title = tr("Редактировать &текст"),
         .shortcuts = {QKeySequence(Qt::Key_E)},
         .icon = QStringLiteral("text-cursor-input"),
         .run =
             [registry] {
                 if (DocumentView* v = registry->view()) {
                     registry->activate(QStringLiteral("edit"), v);
                 }
             },
         .when = QLatin1String(CommandRegistry::kViewContext),
         .enabled = [registry] { return registry->view() != nullptr && registry->view()->canEdit(); },
         .menu = QStringLiteral("tools"),
         .singleKey = true});
    return out;
}

}  // namespace ot::ui
