#include "ui/tools/comment/comment_tool.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QMenu>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>
#include <iterator>

#include <memory>

#include "core/annot/defaults.h"
#include "core/annot/freetext.h"
#include "services/annot_ops.h"
#include "services/comment_settings.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "services/stamps_catalog.h"
#include "ui/dialogs/stamps_palette.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/comment/inline_text_editor.h"
#include "ui/tools/measure/measure_tool.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/view/tools/view_tool.h"
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
    {CommentAction::TextBox, "commentTextBox", "type"},
    {CommentAction::Callout, "commentCallout", "message-square-quote"},
    {CommentAction::Highlight, "commentHighlight", "highlighter"},
    {CommentAction::Underline, "commentUnderline", "underline"},
    {CommentAction::StrikeOut, "commentStrikeOut", "strikethrough"},
    {CommentAction::Squiggly, "commentSquiggly", "spline"},
};

constexpr ActionRow kEditRows[] = {
    {CommentAction::InsertText, "commentInsertText", "text-cursor"},
    {CommentAction::ReplaceText, "commentReplaceText", "replace"},
};

constexpr ActionRow kShapeRows[] = {
    {CommentAction::Line, "commentLine", "slash"},
    {CommentAction::Arrow, "commentArrow", "move-up-right"},
    {CommentAction::Rectangle, "commentRectangle", "square"},
    {CommentAction::Oval, "commentOval", "circle"},
    {CommentAction::Polygon, "commentPolygon", "pentagon"},
    {CommentAction::Cloud, "commentCloud", "cloud"},
    {CommentAction::PolyLine, "commentPolyLine", "waypoints"},
};

// Концы линии по порядку списка (значения annot::LineEnding).
struct EndingRow {
    annot::LineEnding ending;
    const char* title;
};
constexpr EndingRow kEndingRows[] = {
    {annot::LineEnding::None, QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Нет")},
    {annot::LineEnding::OpenArrow, QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Открытая стрелка")},
    {annot::LineEnding::ClosedArrow, QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Закрытая стрелка")},
    {annot::LineEnding::Square, QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Квадрат")},
    {annot::LineEnding::Circle, QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Круг")},
    {annot::LineEnding::Diamond, QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Ромб")},
    {annot::LineEnding::Butt, QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Черта")},
    {annot::LineEnding::Slash, QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Косая черта")},
    {annot::LineEnding::ROpenArrow, QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Обратная открытая стрелка")},
    {annot::LineEnding::RClosedArrow, QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Обратная закрытая стрелка")},
};

bool closedShape(CommentAction a) {
    return a == CommentAction::Rectangle || a == CommentAction::Oval || a == CommentAction::Polygon ||
           a == CommentAction::Cloud;
}

bool openLine(CommentAction a) {
    return a == CommentAction::Line || a == CommentAction::Arrow || a == CommentAction::PolyLine;
}

constexpr ActionRow kStampRows[] = {
    {CommentAction::Stamp, "commentStamp", "stamp"},
    {CommentAction::Attach, "commentAttach", "paperclip"},
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

DocumentView* CommentTool::view() const { return view_; }

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
        updateStampLabel();
    }));
    // Картинка буфера ставится один раз — подпись снова о штампе каталога.
    connections_.push_back(connect(&m, &CommentMode::created, this, [this] { updateStampLabel(); }));
    connections_.push_back(connect(&view.view().tools(), &ToolController::annotSelectionChanged, this,
                                   [this] { loadFormatFromSelection(); }));
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
    const bool text = textAction(action);
    // Текстовое поле и выноска: «Цвет» — заливка (/C, можно без цвета), «Толщина» — рамка (0 — без рамки).
    color_->setNoneAllowed(text);
    colorLabel_->setText(text ? tr("Заливка:") : tr("Цвет:"));
    widthLabel_->setText(text ? tr("Рамка:") : tr("Толщина:"));
    width_->setMinimum(text ? 0.0 : 0.5);
    color_->setColor(p.color.empty() ? QColor() : QColor::fromRgb(static_cast<QRgb>(p.color.toRgb())));
    width_->setValue(p.borderWidth);
    const bool shape = CommentMode::isDragShape(action) || CommentMode::isPolyShape(action);
    width_->setEnabled(action == CommentAction::Ink || text || shape);
    color_->setEnabled(action != CommentAction::Stamp);  // у штампа цвет — в самом штампе
    styleForm_->setRowVisible(fill_, closedShape(action));
    styleForm_->setRowVisible(lineStart_, openLine(action));
    styleForm_->setRowVisible(lineEnd_, openLine(action));
    fill_->setColor(p.interiorColor.empty() ? QColor()
                                            : QColor::fromRgb(static_cast<QRgb>(p.interiorColor.toRgb())));
    lineStart_->setCurrentIndex(std::max(0, lineStart_->findData(static_cast<int>(p.lineStartStyle))));
    lineEnd_->setCurrentIndex(std::max(0, lineEnd_->findData(static_cast<int>(p.lineEndStyle))));
    opacity_->setValue(std::round(p.opacity * 100));
    const int icon = icon_->findData(QString::fromStdString(p.icon));
    icon_->setCurrentIndex(icon >= 0 ? icon : 0);
    icon_->setEnabled(action == CommentAction::Note);
    if (formatTargets_.empty()) {
        const int font = font_->findData(QString::fromStdString(annot::textFontFamily(p.fontName)));
        font_->setCurrentIndex(font >= 0 ? font : 0);
        fontSize_->setValue(p.fontSize > 0 ? p.fontSize : 12);
        textColor_->setColor(p.textColor.empty() ? QColor()
                                                 : QColor::fromRgb(static_cast<QRgb>(p.textColor.toRgb())));
        align_->setCurrentIndex(std::clamp(p.align, 0, 2));
    }
    const bool format = text || !formatTargets_.empty() ||
                        (view_ != nullptr && InlineTextEditor::active(view_->view()) != nullptr);
    for (QWidget* w : {static_cast<QWidget*>(font_.data()), static_cast<QWidget*>(fontSize_.data()),
                       static_cast<QWidget*>(textColor_.data()), static_cast<QWidget*>(align_.data())}) {
        w->setEnabled(format);
    }
    loadingStyle_ = false;
}

void CommentTool::saveStyle() {
    if (loadingStyle_ || panel_ == nullptr) {
        return;
    }
    const CommentAction action = mode() != nullptr ? mode()->action() : CommentAction::Note;
    Settings settings;
    // Поверх прежнего стиля инструмента: поля панели, которых у действия нет, его не стирают.
    comments::ToolStyle style = comments::toolStyle(settings, CommentMode::actionKey(action));
    style.color.reset();
    if (color_->color().isValid()) {
        style.color =
            annot::AnnotColor::fromRgb(static_cast<std::uint32_t>(color_->color().rgb() & 0xffffff));
    }
    style.opacity = static_cast<float>(opacity_->value() / 100.0);
    const bool shape = CommentMode::isDragShape(action) || CommentMode::isPolyShape(action);
    if (action == CommentAction::Ink || textAction(action) || shape) {
        style.width = static_cast<float>(width_->value());
    }
    if (closedShape(action)) {
        style.fill.reset();
        if (fill_->color().isValid()) {
            style.fill =
                annot::AnnotColor::fromRgb(static_cast<std::uint32_t>(fill_->color().rgb() & 0xffffff));
        }
    }
    if (openLine(action)) {
        style.lineStart = lineStart_->currentData().toInt();
        style.lineEnd = lineEnd_->currentData().toInt();
    }
    if (action == CommentAction::Note) {
        style.icon = icon_->currentData().toString().toStdString();
    }
    if (textAction(action) && formatTargets_.empty()) {
        style.font = font_->currentData().toString().toStdString();
        style.fontSize = static_cast<float>(fontSize_->value());
        if (textColor_->color().isValid()) {
            style.textColor =
                annot::AnnotColor::fromRgb(static_cast<std::uint32_t>(textColor_->color().rgb() & 0xffffff));
        }
        style.align = align_->currentIndex();
    }
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
    if (measureButton_ != nullptr) {
        measureButton_->setEnabled(usable);
    }
    if (paletteButton_ != nullptr) {
        paletteButton_->setEnabled(usable);
        paletteButton_->setToolTip(usable ? tr("Выбрать или создать штамп") : lock_);
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
    // Действий и полей стиля больше, чем высота окна, — панель прокручивается.
    auto* scroll = new QScrollArea();
    scroll->setObjectName(QStringLiteral("commentToolPanel"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    panel_ = scroll;
    auto* body = new QWidget(scroll);
    body->setObjectName(QStringLiteral("commentToolPanelBody"));
    body->setAutoFillBackground(false);
    auto* column = new QVBoxLayout(body);
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
    addCaption(column, tr("Фигуры"));
    for (const ActionRow& row : kShapeRows) {
        addAction(row);
    }
    addCaption(column, tr("Штамп и вложение"));
    addAction(kStampRows[0]);
    stampLabel_ = new QLabel(panel_);
    stampLabel_->setObjectName(QStringLiteral("commentStampCurrent"));
    stampLabel_->setWordWrap(true);
    column->addWidget(stampLabel_);
    ToolButton* palette = addRow(column, QStringLiteral("layout-grid"), tr("Палитра штампов…"));
    palette->setObjectName(QStringLiteral("commentStampPalette"));
    connect(palette, &ToolButton::clicked, this, [this] { showStampPalette(); });
    paletteButton_ = palette;
    addAction(kStampRows[1]);
    updateStampLabel();
    // «Измерить ▾» (5.5; эталон 6.2 п. 3): тот же инструмент, что «Измерить» во «Все инструменты».
    addCaption(column, tr("Измерения"));
    ToolButton* measureButton = addRow(column, QStringLiteral("ruler"), tr("Измерить"));
    measureButton->setObjectName(QStringLiteral("commentMeasure"));
    measureButton->setPopupMode(QToolButton::InstantPopup);
    auto* measureMenu = new QMenu(measureButton);
    measureMenu->setObjectName(QStringLiteral("commentMeasureMenu"));
    for (const MeasureAction action :
         {MeasureAction::Distance, MeasureAction::Perimeter, MeasureAction::Area}) {
        QAction* item = measureMenu->addAction(MeasureMode::actionTitle(action));
        connect(item, &QAction::triggered, this, [this, action] {
            auto* tool = dynamic_cast<MeasureTool*>(registry_.find(QStringLiteral("measure")));
            if (tool != nullptr && view_ != nullptr && registry_.activate(QStringLiteral("measure"), view_)) {
                tool->start(action);
            }
        });
    }
    measureButton->setMenu(measureMenu);
    measureButton_ = measureButton;
    buildStyle(column);
    column->addStretch(1);
    scroll->setWidget(body);
}

void CommentTool::buildStyle(QVBoxLayout* column) {
    addCaption(column, tr("Стиль"));
    auto* form = new QFormLayout();
    styleForm_ = form;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(Theme::sizes().grid);
    color_ = new ColorSwatch(QColor(), panel_);
    color_->setObjectName(QStringLiteral("commentColor"));
    color_->setAccessibleName(tr("Цвет"));
    colorLabel_ = new QLabel(tr("Цвет:"), panel_);
    form->addRow(colorLabel_, color_);
    width_ = new NumberField(panel_);
    width_->setObjectName(QStringLiteral("commentWidth"));
    width_->setRange(0.5, 12);
    width_->setSingleStep(0.5);
    width_->setDecimals(1);
    width_->setUnit(tr("пт"));
    widthLabel_ = new QLabel(tr("Толщина:"), panel_);
    form->addRow(widthLabel_, width_);
    fill_ = new ColorSwatch(QColor(), panel_);
    fill_->setObjectName(QStringLiteral("commentFill"));
    fill_->setAccessibleName(tr("Заливка"));
    fill_->setNoneAllowed(true);
    form->addRow(tr("Заливка:"), fill_);
    lineStart_ = new QComboBox(panel_);
    lineStart_->setObjectName(QStringLiteral("commentLineStart"));
    lineEnd_ = new QComboBox(panel_);
    lineEnd_->setObjectName(QStringLiteral("commentLineEnd"));
    for (const EndingRow& e : kEndingRows) {
        lineStart_->addItem(tr(e.title), static_cast<int>(e.ending));
        lineEnd_->addItem(tr(e.title), static_cast<int>(e.ending));
    }
    form->addRow(tr("Начало линии:"), lineStart_);
    form->addRow(tr("Конец линии:"), lineEnd_);
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
    buildFormat(column);
    pin_ = addRow(column, QStringLiteral("pin"), tr("Оставлять инструмент выбранным"));
    pin_->setObjectName(QStringLiteral("commentPin"));
    pin_->setCheckable(true);
    pin_->setToolTip(tr(
        "После записки, текстового поля, вставки или замены текста инструмент не сменяется на «Выделение»"));
    connect(pin_, &ToolButton::toggled, this, [this](bool on) {
        Settings().set(settings::kCommentKeepTool, on);
        setPinned(on);
    });
    connect(color_, &ColorSwatch::colorChanged, this, [this] { saveStyle(); });
    connect(width_, &NumberField::valueChanged, this, [this] { saveStyle(); });
    connect(opacity_, &NumberField::valueChanged, this, [this] { saveStyle(); });
    connect(fill_, &ColorSwatch::colorChanged, this, [this] { saveStyle(); });
    connect(lineStart_, &QComboBox::currentIndexChanged, this, [this] { saveStyle(); });
    connect(lineEnd_, &QComboBox::currentIndexChanged, this, [this] { saveStyle(); });
    connect(icon_, &QComboBox::currentIndexChanged, this, [this] { saveStyle(); });
    pin_->setChecked(Settings().get(settings::kCommentKeepTool));
    loadStyle();
}

bool CommentTool::textAction(CommentAction action) noexcept {
    return action == CommentAction::TextBox || action == CommentAction::Callout;
}

// «Формат текста» (comment-09): шрифт — четыре семейства с встраиваемым TrueType (core/annot/freetext.h),
// кегль, цвет текста, выравнивание.
void CommentTool::buildFormat(QVBoxLayout* column) {
    addCaption(column, tr("Формат текста"));
    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(Theme::sizes().grid);
    font_ = new QComboBox(panel_);
    font_->setObjectName(QStringLiteral("commentFont"));
    for (const annot::TextFont& f : annot::kTextFonts) {
        font_->addItem(QString::fromUtf8(f.family), QString::fromUtf8(f.family));
    }
    form->addRow(tr("Шрифт:"), font_);
    fontSize_ = new NumberField(panel_);
    fontSize_->setObjectName(QStringLiteral("commentFontSize"));
    fontSize_->setRange(4, 144);
    fontSize_->setSingleStep(1);
    fontSize_->setDecimals(0);
    fontSize_->setUnit(tr("пт"));
    form->addRow(tr("Кегль:"), fontSize_);
    textColor_ = new ColorSwatch(QColor(), panel_);
    textColor_->setObjectName(QStringLiteral("commentTextColor"));
    textColor_->setAccessibleName(tr("Цвет текста"));
    form->addRow(tr("Цвет текста:"), textColor_);
    align_ = new SegmentedBar(panel_);
    align_->setObjectName(QStringLiteral("commentAlign"));
    align_->addSegment(QString(), QStringLiteral("align-left"), tr("По левому краю"));
    align_->addSegment(QString(), QStringLiteral("align-center"), tr("По центру"));
    align_->addSegment(QString(), QStringLiteral("align-right"), tr("По правому краю"));
    align_->setCurrentIndex(0);
    form->addRow(tr("Выравнивание:"), align_);
    column->addLayout(form);
    connect(font_, &QComboBox::currentIndexChanged, this, [this] { formatChanged(); });
    connect(fontSize_, &NumberField::valueChanged, this, [this] { formatChanged(); });
    connect(textColor_, &ColorSwatch::colorChanged, this, [this] { formatChanged(); });
    connect(align_, &SegmentedBar::currentChanged, this, [this] { formatChanged(); });
}

// Формат — открытому редактору поля, выбранным полям FreeText (шаг «Set annotation properties» на каждое) или
// стилю следующего поля.
void CommentTool::formatChanged() {
    if (loadingStyle_ || panel_ == nullptr) {
        return;
    }
    const QString family = font_->currentData().toString();
    const double size = fontSize_->value();
    const QColor color = textColor_->color();
    const int align = align_->currentIndex();
    if (view_ != nullptr) {
        if (InlineTextEditor* editor = InlineTextEditor::active(view_->view())) {
            editor->setFormat(family, size, color, align);
            return;
        }
        if (!formatTargets_.empty()) {
            annot::AnnotPatch patch;
            patch.fontName = family.toStdString();
            patch.fontSize = static_cast<float>(size);
            if (color.isValid()) {
                patch.textColor =
                    annot::AnnotColor::fromRgb(static_cast<std::uint32_t>(color.rgb() & 0xffffff));
            }
            patch.align = align;
            for (const annot::AnnotRef& ref : formatTargets_) {
                annot::update(view_->session(), ref, patch);
            }
            return;
        }
    }
    saveStyle();
}

// Выбранные поля FreeText — цели «Формата текста»: поля панели — из первого; нет — стиль действия.
void CommentTool::loadFormatFromSelection() {
    if (view_ == nullptr || panel_ == nullptr) {
        return;
    }
    formatTargets_.clear();
    for (const annot::AnnotInfo& a : view_->view().tools().annotSelect().selection()) {
        if (a.type == annot::AnnotType::FreeText && !a.locked()) {
            formatTargets_.push_back(a.ref);
        }
    }
    if (formatTargets_.empty()) {
        loadStyle();
        return;
    }
    const QPointer<CommentTool> self(this);
    annot::props(view_->session(), formatTargets_.front())
        .then(this, [self](QFuture<std::optional<annot::AnnotProps>> f) {
            std::optional<annot::AnnotProps> p;
            try {
                p = f.result();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): поля нет — формат прежний
            }
            if (self == nullptr || !p || self->panel_ == nullptr) {
                return;
            }
            self->loadingStyle_ = true;
            const int font =
                self->font_->findData(QString::fromStdString(annot::textFontFamily(p->fontName)));
            self->font_->setCurrentIndex(font >= 0 ? font : 0);
            self->fontSize_->setValue(p->fontSize > 0 ? p->fontSize : 12);
            self->textColor_->setColor(
                p->textColor.empty() ? QColor() : QColor::fromRgb(static_cast<QRgb>(p->textColor.toRgb())));
            self->align_->setCurrentIndex(std::clamp(p->align, 0, 2));
            for (QWidget* w :
                 {static_cast<QWidget*>(self->font_.data()), static_cast<QWidget*>(self->fontSize_.data()),
                  static_cast<QWidget*>(self->textColor_.data()),
                  static_cast<QWidget*>(self->align_.data())}) {
                w->setEnabled(true);
            }
            self->loadingStyle_ = false;
        });
}

// ---------------------------------------------------------------- штамп

void CommentTool::updateStampLabel() {
    if (stampLabel_ == nullptr) {
        return;
    }
    CommentMode* m = mode();
    if (m != nullptr && m->hasClipboardStamp()) {
        stampLabel_->setText(tr("Штамп: картинка из буфера обмена"));
        return;
    }
    const QString id = m != nullptr ? m->stamp() : Settings().get(settings::kCommentStamp);
    const std::optional<stamps::StampEntry> entry = stamps::StampCatalog().find(id);
    stampLabel_->setText(entry ? tr("Штамп: %1").arg(StampsPalette::stampTitle(*entry))
                               : tr("Штамп не выбран"));
}

StampsPalette* CommentTool::showStampPalette() {
    if (view_ == nullptr || !lock_.isEmpty()) {
        return nullptr;
    }
    auto* palette = new StampsPalette(view_->window());
    palette->setAttribute(Qt::WA_DeleteOnClose);
    palette->setWindowModality(Qt::WindowModal);
    if (CommentMode* m = mode()) {
        palette->selectStamp(m->stamp());
    }
    connect(palette, &StampsPalette::stampChosen, this, [this](const QString& id) {
        if (view_ == nullptr) {
            return;
        }
        modeOf(view_->view()).setStamp(id);
        start(CommentAction::Stamp);
        updateStampLabel();
    });
    connect(palette, &StampsPalette::clipboardStampChosen, this, [this](const QImage& image) {
        if (view_ == nullptr) {
            return;
        }
        modeOf(view_->view()).setClipboardStamp(image);
        start(CommentAction::Stamp);
        updateStampLabel();
    });
    connect(palette, &StampsPalette::identityRequested, this, [this] {
        if (auto* w = view_ != nullptr ? qobject_cast<MainWindow*>(view_->window()) : nullptr) {
            w->showPreferences(QStringLiteral("commenting"));
        }
    });
    palette->open();
    return palette;
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
    // Показ/скрытие всех комментариев вкладки (comment-19; визуальный фильтр, не /F Hidden).
    out.push_back(
        {.id = QStringLiteral("comment.toggleAll"),
         .title = tr("&Скрыть все комментарии"),
         .shortcuts = {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_8)},
         .icon = QStringLiteral("eye-off"),
         .run =
             [registry] {
                 if (DocumentView* v = registry->view()) {
                     v->setCommentsHidden(!v->commentsHidden());
                 }
             },
         .enabled = [registry] { return registry->view() != nullptr; },
         .menu = QStringLiteral("view"),
         .checkable = true,
         .checked =
             [registry] { return registry->view() != nullptr && registry->view()->commentsHidden(); }});
    out.push_back({.id = QStringLiteral("tool.textbox"),
                   .title = tr("&Текстовое поле"),
                   .shortcuts = {QKeySequence(Qt::Key_J)},
                   .icon = QStringLiteral("type"),
                   .run = runner(CommentAction::TextBox),
                   .when = QLatin1String(CommandRegistry::kViewContext),
                   .enabled = enabled,
                   .menu = QStringLiteral("tools"),
                   .singleKey = true});
    out.push_back({.id = QStringLiteral("tool.rectangle"),
                   .title = tr("&Прямоугольник"),
                   .shortcuts = {QKeySequence(Qt::Key_R)},
                   .icon = QStringLiteral("square"),
                   .run = runner(CommentAction::Rectangle),
                   .when = QLatin1String(CommandRegistry::kViewContext),
                   .enabled = enabled,
                   .menu = QStringLiteral("tools"),
                   .singleKey = true});
    out.push_back({.id = QStringLiteral("tool.stamp"),
                   .title = tr("Ш&тамп"),
                   .shortcuts = {QKeySequence(Qt::Key_K)},
                   .icon = QStringLiteral("stamp"),
                   .run = runner(CommentAction::Stamp),
                   .when = QLatin1String(CommandRegistry::kViewContext),
                   .enabled = enabled,
                   .menu = QStringLiteral("tools"),
                   .singleKey = true});
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
