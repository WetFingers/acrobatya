#include "ui/tools/fillsign/fillsign_tool.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <cmath>

#include "core/annot/freetext.h"
#include "services/annot_ops.h"
#include "services/document_session.h"
#include "services/fillsign_ops.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/save_flow.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/comment/inline_text_editor.h"
#include "ui/tools/fillsign/signature_capture_dialog.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/color_swatch.h"
#include "ui/widgets/number_field.h"
#include "ui/widgets/toast.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

namespace {

struct MarkRow {
    FillSignAction action;
    const char* name;  // objectName кнопки
    const char* icon;
};

constexpr MarkRow kMarkRows[] = {
    {FillSignAction::Check, "fillSignCheck", "check"},
    {FillSignAction::Cross, "fillSignCross", "x"},
    {FillSignAction::Dot, "fillSignDot", "dot"},
    {FillSignAction::Line, "fillSignLine", "minus"},
    {FillSignAction::Box, "fillSignBox", "rectangle-horizontal"},
};

FillSignAction fillSignActionOf(fillsign::SignatureKind kind) {
    return kind == fillsign::SignatureKind::Initials ? FillSignAction::Initials : FillSignAction::Signature;
}

}  // namespace

FillSignTool::FillSignTool(ToolRegistry& registry) : registry_(registry) {}

// Реестр — статический: после QApplication панель не удаляется (её окно уже закрыто).
FillSignTool::~FillSignTool() {
    if (QCoreApplication::instance() != nullptr) {
        delete panel_.data();
    }
}

QString FillSignTool::title() const {
    return QCoreApplication::translate("ot::ui::ToolRegistry", "Заполнить и подписать");
}

QString FillSignTool::icon() const { return QLatin1String(toolIconName("fillsign")); }

FillSignMode& FillSignTool::modeOf(PageView& view) {
    ToolController& tools = view.tools();
    if (auto* mode = dynamic_cast<FillSignMode*>(tools.find(ViewToolId::FillSign))) {
        return *mode;
    }
    return *static_cast<FillSignMode*>(tools.install(std::make_unique<FillSignMode>(view, tools)));
}

DocumentView* FillSignTool::view() const { return view_; }

FillSignMode* FillSignTool::mode() const {
    if (view_ == nullptr) {
        return nullptr;
    }
    return dynamic_cast<FillSignMode*>(view_->view().tools().find(ViewToolId::FillSign));
}

void FillSignTool::activate(DocumentView& view) {
    for (const QMetaObject::Connection& c : connections_) {
        QObject::disconnect(c);
    }
    connections_.clear();
    view_ = &view;
    FillSignMode& m = modeOf(view.view());
    connections_.push_back(connect(&view, &DocumentView::readOnlyChanged, this, [this] { updateLock(); }));
    connections_.push_back(
        connect(&view, &DocumentView::protectedViewChanged, this, [this] { updateLock(); }));
    connections_.push_back(connect(&view.session(), &DocumentSession::saved, this, [this] {
        updateLock();
        updateNotes();
    }));
    connections_.push_back(connect(&m, &FillSignMode::failed, this, [this](const QString& message) {
        if (view_ != nullptr) {
            Toast::notify(view_, tr("Не добавлено: %1").arg(message), Tone::Danger);
        }
    }));
    connections_.push_back(
        connect(&view.view().tools(), &ToolController::toolChanged, this, [this] { syncButtons(); }));
    connections_.push_back(connect(&m, &FillSignMode::actionChanged, this, [this] { syncButtons(); }));
    m.setInkColor(color_ != nullptr ? color_->color() : QColor(Qt::black));
    if (fontSize_ != nullptr) {
        m.setFontSize(static_cast<float>(fontSize_->value()));
    }
    updateLock();
    updateNotes();
    start(m.action());  // инструмент выбран — сразу «Добавить текст» (или прежнее действие)
}

void FillSignTool::deactivate() {
    for (const QMetaObject::Connection& c : connections_) {
        QObject::disconnect(c);
    }
    connections_.clear();
    if (QCoreApplication::instance() == nullptr) {
        return;  // реестр — статический: после QApplication виджетов уже не трогать
    }
    if (view_ != nullptr) {
        ToolController& tools = view_->view().tools();
        if (tools.tool() == ViewToolId::FillSign) {
            tools.setTool(ToolController::defaultTool());
        }
    }
    view_ = nullptr;
    syncButtons();
}

void FillSignTool::start(FillSignAction action) {
    if (view_ == nullptr || !lock_.isEmpty()) {
        return;
    }
    PageView& pv = view_->view();
    FillSignMode& m = modeOf(pv);
    m.setAction(action);
    pv.tools().setTool(ViewToolId::FillSign);
    pv.setFocus();
    syncButtons();
}

void FillSignTool::updateLock() {
    QString reason;
    if (view_ != nullptr) {
        const DocumentView& v = *view_;
        if (v.isViewOnly()) {
            reason = tr("Заполнять можно только PDF: сохраните файл как PDF.");
        } else if (v.isProtectedView()) {
            reason = tr("Защищённый просмотр: заполнение отключено. Нажмите «Включить все функции».");
        } else if (v.isReadOnly()) {
            reason = tr("Документ открыт только для чтения: нажмите «Включить редактирование».");
        } else if (!v.permissions().annotate) {
            reason = tr("Документ защищён: добавлять текст и подписи запрещено.");
        }
    }
    if (FillSignMode* m = mode()) {
        m->setLocked(reason);
        if (!reason.isEmpty() && view_->view().tools().tool() == ViewToolId::FillSign) {
            view_->view().tools().setTool(ToolController::defaultTool());
        }
    }
    if (reason != lock_) {
        lock_ = reason;
        emit lockChanged(reason);
    }
    syncButtons();
}

// Поля AcroForm — «Заполнить форму» (6.1); подписанный — запись инкрементом с предупреждением.
void FillSignTool::updateNotes() {
    if (panel_ == nullptr) {
        return;
    }
    const OpenSummary summary = view_ != nullptr ? view_->session().summary() : OpenSummary{};
    formHint_->setVisible(view_ != nullptr && summary.hasForm);
    fillForm_->setVisible(view_ != nullptr && summary.hasForm);
    signedNote_->setVisible(view_ != nullptr && summary.isSigned);
}

void FillSignTool::syncButtons() {
    if (panel_ == nullptr) {
        return;
    }
    const bool usable = view_ != nullptr && lock_.isEmpty();
    FillSignMode* m = mode();
    const bool active = usable && m != nullptr && view_->view().tools().tool() == ViewToolId::FillSign;
    for (const auto& [action, button] : buttons_) {
        if (button == nullptr) {
            continue;
        }
        button->setEnabled(usable);
        button->setChecked(active && m->action() == action);
        button->setToolTip(usable ? FillSignMode::actionTitle(action) : lock_);
    }
    if (flatten_ != nullptr) {
        flatten_->setEnabled(view_ != nullptr && !view_->isViewOnly());
    }
    for (QWidget* w : {static_cast<QWidget*>(fontSize_.data()), static_cast<QWidget*>(color_.data())}) {
        if (w != nullptr) {
            w->setEnabled(usable);
        }
    }
    if (lockLabel_ != nullptr) {
        lockLabel_->setText(lock_);
        lockLabel_->setVisible(!lock_.isEmpty());
    }
}

ToolButton* FillSignTool::actionButton(FillSignAction action) const {
    const auto it = buttons_.find(action);
    return it == buttons_.end() ? nullptr : it->second.data();
}

// ---------------------------------------------------------------- панель

void FillSignTool::addCaption(QVBoxLayout* column, const QString& text) {
    auto* caption = new QLabel(text, panel_);
    caption->setObjectName(QStringLiteral("fillSignSectionCaption"));
    column->addSpacing(Theme::sizes().grid * 2);
    column->addWidget(caption);
}

ToolButton* FillSignTool::addRow(QVBoxLayout* column, const QString& icon, const QString& label) {
    auto* b = new ToolButton(icon, label, panel_);
    b->setLabelVisible(true);
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    column->addWidget(b);
    return b;
}

QWidget* FillSignTool::panel() {
    if (panel_ == nullptr) {
        buildPanel();
        updateNotes();
        syncButtons();
    }
    return panel_;
}

void FillSignTool::buildPanel() {
    const Metrics& m = Theme::sizes();
    auto* scroll = new QScrollArea();
    scroll->setObjectName(QStringLiteral("fillSignToolPanel"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    panel_ = scroll;
    auto* body = new QWidget(scroll);
    body->setObjectName(QStringLiteral("fillSignToolPanelBody"));
    body->setAutoFillBackground(false);
    auto* column = new QVBoxLayout(body);
    column->setContentsMargins(m.grid * 3, m.grid * 3, m.grid * 3, m.grid * 3);
    column->setSpacing(m.grid);

    lockLabel_ = new QLabel(panel_);
    lockLabel_->setObjectName(QStringLiteral("fillSignLockNote"));
    lockLabel_->setWordWrap(true);
    lockLabel_->setVisible(false);
    column->addWidget(lockLabel_);
    formHint_ = new QLabel(tr("В документе есть поля формы: их удобнее заполнить как форму. Текст и подписи "
                              "можно ставить и поверх страницы."),
                           panel_);
    formHint_->setObjectName(QStringLiteral("fillSignFormHint"));
    formHint_->setWordWrap(true);
    column->addWidget(formHint_);
    fillForm_ = new QPushButton(tr("Заполнить &форму"), panel_);
    fillForm_->setObjectName(QStringLiteral("fillSignFillForm"));
    connect(fillForm_, &QPushButton::clicked, this, [this] {
        if (auto* w = view_ != nullptr ? qobject_cast<MainWindow*>(view_->window()) : nullptr) {
            w->notifyLater(tr("Заполнение полей формы"), 6);
        }
    });
    column->addWidget(fillForm_, 0, Qt::AlignLeft);
    signedNote_ =
        new QLabel(tr("Документ подписан. Подпись останется действительной, но Acrobat Reader покажет, "
                      "что документ изменён после подписания."),
                   panel_);
    signedNote_->setObjectName(QStringLiteral("fillSignSignedNote"));
    signedNote_->setWordWrap(true);
    column->addWidget(signedNote_);

    addCaption(column, tr("Заполнить"));
    ToolButton* text =
        addRow(column, QStringLiteral("type"), FillSignMode::actionTitle(FillSignAction::Text));
    text->setObjectName(QStringLiteral("fillSignText"));
    text->setCheckable(true);
    connect(text, &ToolButton::clicked, this, [this] { start(FillSignAction::Text); });
    buttons_[FillSignAction::Text] = text;
    // Отметки — рядом одной строкой, как у Acrobat.
    auto* marks = new QHBoxLayout();
    marks->setSpacing(m.grid);
    for (const MarkRow& row : kMarkRows) {
        auto* b = new ToolButton(QLatin1String(row.icon), FillSignMode::actionTitle(row.action), panel_);
        b->setObjectName(QLatin1String(row.name));
        b->setCheckable(true);
        const FillSignAction action = row.action;
        connect(b, &ToolButton::clicked, this, [this, action] { start(action); });
        buttons_[action] = b;
        marks->addWidget(b);
    }
    marks->addStretch(1);
    column->addLayout(marks);

    addCaption(column, tr("Подписать"));
    for (const fillsign::SignatureKind kind :
         {fillsign::SignatureKind::Signature, fillsign::SignatureKind::Initials}) {
        const bool initials = kind == fillsign::SignatureKind::Initials;
        ToolButton* b = addRow(column, initials ? QStringLiteral("case-upper") : QStringLiteral("signature"),
                               initials ? tr("Инициалы") : tr("Подпись"));
        b->setObjectName(initials ? QStringLiteral("fillSignInitials") : QStringLiteral("fillSignSignature"));
        b->setCheckable(true);
        b->setPopupMode(QToolButton::InstantPopup);
        b->setMenu(signatureMenu(kind));
        buttons_[fillSignActionOf(kind)] = b;
    }

    addCaption(column, tr("Стиль"));
    auto* style = new QHBoxLayout();
    style->setSpacing(m.grid * 2);
    style->addWidget(new QLabel(tr("Кегль:"), panel_));
    fontSize_ = new NumberField(panel_);
    fontSize_->setObjectName(QStringLiteral("fillSignFontSize"));
    fontSize_->setRange(fillsign::kMinFontSize, 72);
    fontSize_->setSingleStep(1);
    fontSize_->setDecimals(0);
    fontSize_->setUnit(tr("пт"));
    fontSize_->setValue(fillsign::kDefaultFontSize);
    fontSize_->setToolTip(tr("Текст на линиях и в клетках бланка подбирается под их высоту сам"));
    style->addWidget(fontSize_);
    style->addWidget(new QLabel(tr("Цвет:"), panel_));
    color_ = new ColorSwatch(QColor(Qt::black), panel_);
    color_->setObjectName(QStringLiteral("fillSignColor"));
    color_->setAccessibleName(tr("Цвет текста и отметок"));
    style->addWidget(color_);
    style->addStretch(1);
    column->addLayout(style);
    connect(fontSize_, &NumberField::valueChanged, this, [this] { styleChanged(); });
    connect(color_, &ColorSwatch::colorChanged, this, [this] { styleChanged(); });

    addCaption(column, tr("Сохранить"));
    flatten_ = addRow(column, QStringLiteral("layers"), tr("Плоская копия…"));
    flatten_->setObjectName(QStringLiteral("fillSignFlatten"));
    flatten_->setToolTip(tr("Сохранить копию, в которой текст, отметки и подписи — часть страниц"));
    connect(flatten_, &ToolButton::clicked, this, [this] { saveFlatCopy(); });
    column->addStretch(1);
    scroll->setWidget(body);
}

// Стиль — новому тексту и отметкам, открытому редактору поля и выбранным полям FreeText.
void FillSignTool::styleChanged() {
    if (loading_ || panel_ == nullptr) {
        return;
    }
    const QColor color = color_->color();
    const auto size = static_cast<float>(fontSize_->value());
    FillSignMode* m = mode();
    if (m != nullptr) {
        m->setInkColor(color);
        m->setFontSize(size);
    }
    if (view_ == nullptr) {
        return;
    }
    if (InlineTextEditor* editor = InlineTextEditor::active(view_->view())) {
        editor->setFormat(QString::fromUtf8(annot::textFontFamily(editor->props().fontName).c_str()), size,
                          color, editor->props().align);
        return;
    }
    // Выбранное: тексту и отметкам — цвет текста, подписи и инициалам — цвет картинки.
    for (const annot::AnnotInfo& a : view_->view().tools().annotSelect().selection()) {
        if (a.locked()) {
            continue;
        }
        if (a.type == annot::AnnotType::FreeText) {
            annot::AnnotPatch patch;
            patch.textColor = annot::AnnotColor::fromRgb(static_cast<std::uint32_t>(color.rgb() & 0xffffff));
            annot::update(view_->session(), a.ref, patch);
        } else if (a.type == annot::AnnotType::Stamp) {
            fillsign::setSignatureColor(view_->session(), a.ref, color);  // не подпись Fill & Sign — без шага
        }
    }
}

void FillSignTool::saveFlatCopy() {
    if (view_ == nullptr) {
        return;
    }
    if (auto* w = qobject_cast<MainWindow*>(view_->window())) {
        if (InlineTextEditor* editor = InlineTextEditor::active(view_->view())) {
            editor->commit();  // набранное — в копию
        }
        w->saveFlow().saveFlatCopy(*view_);
    }
}

// ---------------------------------------------------------------- подпись и инициалы

QMenu* FillSignTool::signatureMenu(fillsign::SignatureKind kind) {
    QPointer<QMenu>& menu = menus_[kind];
    if (menu == nullptr) {
        menu = new QMenu(panel_);
        menu->setObjectName(kind == fillsign::SignatureKind::Initials
                                ? QStringLiteral("fillSignInitialsMenu")
                                : QStringLiteral("fillSignSignatureMenu"));
        connect(menu, &QMenu::aboutToShow, this, [this, kind] { fillSignatureMenu(kind); });
        fillSignatureMenu(kind);
    }
    return menu;
}

// Сохранённая картинка — кнопкой с миниатюрой и крестиком «Удалить»; «Добавить…»/«Заменить…».
void FillSignTool::fillSignatureMenu(fillsign::SignatureKind kind) {
    QMenu* menu = menus_[kind];
    if (menu == nullptr) {
        return;
    }
    menu->clear();
    const Metrics& m = Theme::sizes();
    const bool initials = kind == fillsign::SignatureKind::Initials;
    const QImage saved = fillsign::SavedSignatures().load(kind);
    if (!saved.isNull()) {
        auto* row = new QWidget(menu);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(m.grid * 2, m.grid, m.grid * 2, m.grid);
        layout->setSpacing(m.grid * 2);
        auto* use = new QToolButton(row);
        use->setObjectName(initials ? QStringLiteral("fillSignUseInitials")
                                    : QStringLiteral("fillSignUseSignature"));
        use->setAutoRaise(true);
        const QSize thumb(m.leftPanel - m.grid * 24, m.row * 3 / 2);
        use->setIcon(QPixmap::fromImage(saved.scaled(thumb, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        use->setIconSize(thumb);
        use->setToolTip(initials ? tr("Поставить инициалы") : tr("Поставить подпись"));
        use->setAccessibleName(use->toolTip());
        connect(use, &QToolButton::clicked, this, [this, kind, menu] {
            menu->close();
            useSaved(kind);
        });
        layout->addWidget(use, 1);
        auto* remove = new QToolButton(row);
        remove->setObjectName(initials ? QStringLiteral("fillSignRemoveInitials")
                                       : QStringLiteral("fillSignRemoveSignature"));
        remove->setAutoRaise(true);
        remove->setIcon(Icons::icon(QStringLiteral("x")));
        remove->setToolTip(initials ? tr("Удалить сохранённые инициалы") : tr("Удалить сохранённую подпись"));
        remove->setAccessibleName(remove->toolTip());
        connect(remove, &QToolButton::clicked, this, [this, kind, menu] {
            menu->close();
            removeSaved(kind);
        });
        layout->addWidget(remove);
        auto* action = new QWidgetAction(menu);
        action->setDefaultWidget(row);
        menu->addAction(action);
        menu->addSeparator();
    }
    QAction* add =
        menu->addAction(Icons::icon(QStringLiteral("pen-line")),
                        saved.isNull() ? (initials ? tr("Добавить инициалы…") : tr("Добавить подпись…"))
                                       : (initials ? tr("Заменить инициалы…") : tr("Заменить подпись…")));
    add->setObjectName(initials ? QStringLiteral("fillSignAddInitials")
                                : QStringLiteral("fillSignAddSignature"));
    connect(add, &QAction::triggered, this, [this, kind] { addSignature(kind); });
}

void FillSignTool::useSaved(fillsign::SignatureKind kind) {
    if (view_ == nullptr || !lock_.isEmpty()) {
        return;
    }
    if (!fillsign::SavedSignatures().has(kind)) {
        addSignature(kind);
        return;
    }
    modeOf(view_->view()).setSignatureImage(kind, {});
    start(fillSignActionOf(kind));
}

SignatureCaptureDialog* FillSignTool::addSignature(fillsign::SignatureKind kind) {
    if (view_ == nullptr || !lock_.isEmpty()) {
        return nullptr;
    }
    SignatureCaptureDialog* dialog = SignatureCaptureDialog::capture(kind, view_->window());
    connect(dialog, &SignatureCaptureDialog::captured, this,
            [this](fillsign::SignatureKind captured, const QImage& image) {
                if (view_ == nullptr) {
                    return;
                }
                modeOf(view_->view()).setSignatureImage(captured, image);
                start(fillSignActionOf(captured));
            });
    return dialog;
}

bool FillSignTool::removeSaved(fillsign::SignatureKind kind) {
    const bool removed = fillsign::SavedSignatures().remove(kind);
    if (view_ != nullptr) {
        modeOf(view_->view()).setSignatureImage(kind, {});
        Toast::notify(view_,
                      kind == fillsign::SignatureKind::Initials ? tr("Сохранённые инициалы удалены.")
                                                                : tr("Сохранённая подпись удалена."),
                      Tone::Ok);
    }
    return removed;
}

}  // namespace ot::ui
