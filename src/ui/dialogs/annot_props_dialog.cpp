#include "ui/dialogs/annot_props_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFuture>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <iterator>

#include "core/annot/defaults.h"
#include "core/annot/freetext.h"
#include "services/annot_ops.h"
#include "services/document_session.h"
#include "ui/theme/theme.h"
#include "ui/tools/comment/annot_menu.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/color_swatch.h"
#include "ui/widgets/number_field.h"
#include "ui/widgets/property_sheet.h"
#include "ui/widgets/segmented_bar.h"

namespace ot::ui {

namespace {

using annot::AnnotType;

QColor propsQColor(const annot::AnnotColor& c) {
    return c.empty() ? QColor() : QColor::fromRgb(static_cast<QRgb>(c.toRgb()));
}

annot::AnnotColor propsColor(const QColor& c) {
    return c.isValid() ? annot::AnnotColor::fromRgb(static_cast<std::uint32_t>(c.rgb() & 0xffffff))
                       : annot::AnnotColor{};
}

bool propsSameColor(const annot::AnnotColor& a, const annot::AnnotColor& b) {
    return a.empty() == b.empty() && (a.empty() || a.toRgb() == b.toRgb());
}

// Значки записки (порядок и подписи — как в панели инструмента, контекст переводов общий).
constexpr const char* kPropsNoteIcons[] = {QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Комментарий"),
                                           QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Ключ"),
                                           QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Записка"),
                                           QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Справка"),
                                           QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Новый абзац"),
                                           QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Абзац"),
                                           QT_TRANSLATE_NOOP("ot::ui::CommentTool", "Вставка")};

// Значки вложения (/Name у FileAttachment).
struct PropsIconRow {
    const char* name;
    const char* title;
};
constexpr PropsIconRow kPropsAttachIcons[] = {
    {"PushPin", QT_TRANSLATE_NOOP("ot::ui::AnnotPropsDialog", "Булавка")},
    {"Paperclip", QT_TRANSLATE_NOOP("ot::ui::AnnotPropsDialog", "Скрепка")},
    {"Graph", QT_TRANSLATE_NOOP("ot::ui::AnnotPropsDialog", "График")},
    {"Tag", QT_TRANSLATE_NOOP("ot::ui::AnnotPropsDialog", "Ярлык")},
};

// Концы линии (контекст переводов — как в панели инструмента).
struct PropsEndingRow {
    annot::LineEnding ending;
    const char* title;
};
constexpr PropsEndingRow kPropsEndings[] = {
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

// Штрихи стиля линии (пункты): штрих и пунктир.
const std::vector<float> kPropsDashed{4, 2};
const std::vector<float> kPropsDotted{1, 2};

bool propsClosedShape(AnnotType t) {
    return t == AnnotType::Square || t == AnnotType::Circle || t == AnnotType::Polygon;
}
bool propsLineLike(AnnotType t) { return t == AnnotType::Line || t == AnnotType::PolyLine; }
bool propsHasWidth(AnnotType t) {
    return propsClosedShape(t) || propsLineLike(t) || t == AnnotType::Ink || t == AnnotType::FreeText;
}

// Лист свойств прижат кверху (строки не растягиваются на высоту вкладки).
QWidget* topAligned(QWidget* sheet) {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(sheet);
    layout->addStretch(1);
    return page;
}

QLabel* propsValueLabel(QWidget* parent) {
    auto* l = new QLabel(parent);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

}  // namespace

QString AnnotPropsDialog::typeTitle(const annot::AnnotProps& p) {
    const char* title = nullptr;
    switch (p.type) {
        case AnnotType::Text:
            title = QT_TR_NOOP("Записка");
            break;
        case AnnotType::FreeText:
            title = p.intent == "FreeTextCallout" ? QT_TR_NOOP("Текстовая выноска")
                                                  : QT_TR_NOOP("Текстовое поле");
            break;
        case AnnotType::Line:
            title = p.intent == "LineArrow" ? QT_TR_NOOP("Стрелка") : QT_TR_NOOP("Линия");
            break;
        case AnnotType::Square:
            title = QT_TR_NOOP("Прямоугольник");
            break;
        case AnnotType::Circle:
            title = QT_TR_NOOP("Овал");
            break;
        case AnnotType::Polygon:
            title = p.cloudy ? QT_TR_NOOP("Облако") : QT_TR_NOOP("Многоугольник");
            break;
        case AnnotType::PolyLine:
            title = QT_TR_NOOP("Ломаная");
            break;
        case AnnotType::Highlight:
            title = QT_TR_NOOP("Выделение текста");
            break;
        case AnnotType::Underline:
            title = QT_TR_NOOP("Подчёркивание текста");
            break;
        case AnnotType::StrikeOut:
            title = QT_TR_NOOP("Зачёркивание текста");
            break;
        case AnnotType::Squiggly:
            title = QT_TR_NOOP("Волнистое подчёркивание");
            break;
        case AnnotType::Caret:
            title = QT_TR_NOOP("Вставка текста");
            break;
        case AnnotType::Ink:
            title = QT_TR_NOOP("Карандаш");
            break;
        case AnnotType::Stamp:
            title = QT_TR_NOOP("Штамп");
            break;
        case AnnotType::FileAttachment:
            title = QT_TR_NOOP("Вложенный файл");
            break;
        default:
            title = QT_TR_NOOP("Комментарий");
            break;
    }
    return QCoreApplication::translate("ot::ui::AnnotPropsDialog", title);
}

AnnotPropsDialog::AnnotPropsDialog(PageView& view, const annot::AnnotRef& ref, QWidget* parent)
    : QDialog(parent), view_(view), session_(view.document()), ref_(ref) {
    setObjectName(QStringLiteral("annotPropsDialog"));
    setWindowTitle(tr("Свойства комментария"));
    const Metrics& m = Theme::sizes();
    resize(m.propsDialogWidth, m.propsDialogHeight);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 4, m.grid * 4, m.grid * 4, m.grid * 4);
    column->setSpacing(m.grid * 3);
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("annotPropsTabs"));
    tabs_->addTab(buildAppearance(), tr("&Вид"));
    tabs_->addTab(buildGeneral(), tr("&Общие"));
    tabs_->addTab(buildHistory(), tr("&История проверок"));
    column->addWidget(tabs_, 1);

    locked_ = new QCheckBox(tr("&Заблокировано"), this);
    locked_->setObjectName(QStringLiteral("annotPropsLocked"));
    locked_->setToolTip(tr("Комментарий нельзя переместить, изменить размер и удалить"));
    makeDefault_ = new QCheckBox(tr("Сделать свойствами по &умолчанию"), this);
    makeDefault_->setObjectName(QStringLiteral("annotPropsMakeDefault"));
    makeDefault_->setToolTip(tr("Следующие комментарии этого типа получат эти цвет, толщину и шрифт"));
    column->addWidget(locked_);
    column->addWidget(makeDefault_);
    auto* buttons = new QDialogButtonBox(this);
    ok_ = buttons->addButton(QDialogButtonBox::Ok);
    ok_->setDefault(true);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &AnnotPropsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
    setEnabled(false);  // до загрузки

    if (session_ != nullptr) {
        const QPointer<AnnotPropsDialog> self(this);
        annot::props(*session_, ref).then(this, [self](QFuture<std::optional<annot::AnnotProps>> f) {
            std::optional<annot::AnnotProps> p;
            try {
                p = f.result();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет аннотации — закрыть
            }
            if (self == nullptr) {
                return;
            }
            if (!p) {
                self->reject();
                return;
            }
            self->fill(*p);
        });
    }
}

AnnotPropsDialog* AnnotPropsDialog::openFor(PageView& view, const annot::AnnotRef& ref) {
    auto* dialog = new AnnotPropsDialog(view, ref, view.window());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
    return dialog;
}

void AnnotPropsDialog::showTab(Tab tab) { tabs_->setCurrentIndex(tab); }

QWidget* AnnotPropsDialog::buildAppearance() {
    auto* sheet = new PropertySheet();
    sheet->setObjectName(QStringLiteral("annotPropsAppearance"));
    auto row = [this, sheet](const QString& label, QWidget* editor) {
        QLabel* l = sheet->addRow(label, editor);
        rows_.emplace_back(editor, l);
        return l;
    };
    sheet->addSection(tr("Линия и заливка"));
    color_ = new ColorSwatch(QColor(), sheet);
    color_->setObjectName(QStringLiteral("annotPropsColor"));
    colorLabel_ = row(tr("&Цвет:"), color_);
    fill_ = new ColorSwatch(QColor(), sheet);
    fill_->setObjectName(QStringLiteral("annotPropsFill"));
    fill_->setNoneAllowed(true);
    row(tr("&Заливка:"), fill_);
    width_ = new NumberField(sheet);
    width_->setObjectName(QStringLiteral("annotPropsWidth"));
    width_->setRange(0, 12);
    width_->setSingleStep(0.5);
    width_->setDecimals(1);
    width_->setUnit(tr("пт"));
    widthLabel_ = row(tr("&Толщина:"), width_);
    lineStyle_ = new QComboBox(sheet);
    lineStyle_->setObjectName(QStringLiteral("annotPropsLineStyle"));
    row(tr("&Стиль линии:"), lineStyle_);
    opacity_ = new NumberField(sheet);
    opacity_->setObjectName(QStringLiteral("annotPropsOpacity"));
    opacity_->setRange(5, 100);
    opacity_->setSingleStep(5);
    opacity_->setDecimals(0);
    opacity_->setUnit(QStringLiteral("%"));
    row(tr("&Непрозрачность:"), opacity_);
    icon_ = new QComboBox(sheet);
    icon_->setObjectName(QStringLiteral("annotPropsIcon"));
    row(tr("Зна&чок:"), icon_);
    lineStart_ = new QComboBox(sheet);
    lineStart_->setObjectName(QStringLiteral("annotPropsLineStart"));
    lineEnd_ = new QComboBox(sheet);
    lineEnd_->setObjectName(QStringLiteral("annotPropsLineEnd"));
    for (const PropsEndingRow& e : kPropsEndings) {
        lineStart_->addItem(QCoreApplication::translate("ot::ui::CommentTool", e.title),
                            static_cast<int>(e.ending));
        lineEnd_->addItem(QCoreApplication::translate("ot::ui::CommentTool", e.title),
                          static_cast<int>(e.ending));
    }
    row(tr("&Начало линии:"), lineStart_);
    row(tr("&Конец линии:"), lineEnd_);
    textSection_ = sheet->addSection(tr("Текст"));
    font_ = new QComboBox(sheet);
    font_->setObjectName(QStringLiteral("annotPropsFont"));
    for (const annot::TextFont& f : annot::kTextFonts) {
        font_->addItem(QString::fromUtf8(f.family), QString::fromUtf8(f.family));
    }
    row(tr("&Шрифт:"), font_);
    fontSize_ = new NumberField(sheet);
    fontSize_->setObjectName(QStringLiteral("annotPropsFontSize"));
    fontSize_->setRange(4, 144);
    fontSize_->setDecimals(0);
    fontSize_->setUnit(tr("пт"));
    row(tr("&Кегль:"), fontSize_);
    textColor_ = new ColorSwatch(QColor(), sheet);
    textColor_->setObjectName(QStringLiteral("annotPropsTextColor"));
    row(tr("Цвет &текста:"), textColor_);
    align_ = new SegmentedBar(sheet);
    align_->setObjectName(QStringLiteral("annotPropsAlign"));
    align_->addSegment(QString(), QStringLiteral("align-left"), tr("По левому краю"));
    align_->addSegment(QString(), QStringLiteral("align-center"), tr("По центру"));
    align_->addSegment(QString(), QStringLiteral("align-right"), tr("По правому краю"));
    row(tr("&Выравнивание:"), align_);
    return topAligned(sheet);
}

QWidget* AnnotPropsDialog::buildGeneral() {
    auto* sheet = new PropertySheet();
    sheet->setObjectName(QStringLiteral("annotPropsGeneral"));
    sheet->addSection(tr("Общие"));
    author_ = new QLineEdit(sheet);
    author_->setObjectName(QStringLiteral("annotPropsAuthor"));
    sheet->addRow(tr("&Автор:"), author_);
    subject_ = new QLineEdit(sheet);
    subject_->setObjectName(QStringLiteral("annotPropsSubject"));
    sheet->addRow(tr("Т&ема:"), subject_);
    type_ = propsValueLabel(sheet);
    sheet->addRow(tr("Тип:"), type_);
    modified_ = propsValueLabel(sheet);
    modified_->setObjectName(QStringLiteral("annotPropsModified"));
    sheet->addRow(tr("Изменён:"), modified_);
    return topAligned(sheet);
}

QWidget* AnnotPropsDialog::buildHistory() {
    auto* page = new QWidget();
    page->setObjectName(QStringLiteral("annotPropsHistory"));
    auto* layout = new QVBoxLayout(page);
    history_ =
        new QLabel(tr("Статусов рецензии у комментария нет. Здесь появится история: кто и когда поставил "
                      "«Принято», «Отклонено», «Отменено» или «Завершено»."),
                   page);
    history_->setObjectName(QStringLiteral("annotPropsHistoryEmpty"));
    history_->setWordWrap(true);
    layout->addWidget(history_);
    layout->addStretch(1);
    return page;
}

void AnnotPropsDialog::setRowVisible(QWidget* editor, bool visible) {
    for (auto& [w, label] : rows_) {
        if (w == editor) {
            w->setVisible(visible);
            label->setVisible(visible);
        }
    }
}

bool AnnotPropsDialog::rowVisible(QWidget* editor) const { return editor != nullptr && !editor->isHidden(); }

AnnotPropsDialog::LineStyle AnnotPropsDialog::lineStyleOf(const annot::AnnotProps& p) const {
    if (p.cloudy) {
        return p.cloudIntensity >= 1.5f ? Cloud2 : Cloud1;
    }
    if (!p.dash.empty()) {
        return p.dash.front() <= 1.5f ? Dotted : Dashed;
    }
    return Solid;
}

void AnnotPropsDialog::fill(const annot::AnnotProps& p) {
    props_ = p;
    const AnnotType t = p.type;
    const bool freeText = t == AnnotType::FreeText;
    setWindowTitle(tr("Свойства: %1").arg(typeTitle(p)));

    // Вид — по типу.
    colorLabel_->setText(freeText ? tr("&Заливка:") : tr("&Цвет:"));
    color_->setNoneAllowed(freeText);
    color_->setColor(propsQColor(p.color));
    setRowVisible(color_, t != AnnotType::Stamp);
    fill_->setColor(propsQColor(p.interiorColor));
    setRowVisible(fill_, propsClosedShape(t) || t == AnnotType::Line);
    widthLabel_->setText(freeText ? tr("&Рамка:") : tr("&Толщина:"));
    width_->setValue(p.borderWidth);
    setRowVisible(width_, propsHasWidth(t));
    lineStyle_->clear();
    lineStyle_->addItem(tr("Сплошная"), Solid);
    lineStyle_->addItem(tr("Штрихи"), Dashed);
    lineStyle_->addItem(tr("Пунктир"), Dotted);
    if (propsClosedShape(t)) {
        lineStyle_->addItem(tr("Облако"), Cloud1);
        lineStyle_->addItem(tr("Облако крупное"), Cloud2);
    }
    lineStyle_->setCurrentIndex(std::max(0, lineStyle_->findData(lineStyleOf(p))));
    setRowVisible(lineStyle_, propsClosedShape(t) || propsLineLike(t) || freeText);
    opacity_->setValue(std::round(p.opacity * 100));
    icon_->clear();
    if (t == AnnotType::Text) {
        for (std::size_t i = 0; i < std::size(annot::kNoteIcons); ++i) {
            icon_->addItem(QCoreApplication::translate("ot::ui::CommentTool", kPropsNoteIcons[i]),
                           QLatin1String(annot::kNoteIcons[i]));
        }
    } else if (t == AnnotType::FileAttachment) {
        for (const PropsIconRow& r : kPropsAttachIcons) {
            icon_->addItem(tr(r.title), QLatin1String(r.name));
        }
    }
    const int icon = icon_->findData(QString::fromStdString(p.icon));
    icon_->setCurrentIndex(icon >= 0 ? icon : 0);
    setRowVisible(icon_, t == AnnotType::Text || t == AnnotType::FileAttachment);
    lineStart_->setCurrentIndex(std::max(0, lineStart_->findData(static_cast<int>(p.lineStartStyle))));
    lineEnd_->setCurrentIndex(std::max(0, lineEnd_->findData(static_cast<int>(p.lineEndStyle))));
    setRowVisible(lineStart_, propsLineLike(t) || (freeText && !p.callout.empty()));
    setRowVisible(lineEnd_, propsLineLike(t));
    const int font = font_->findData(QString::fromStdString(annot::textFontFamily(p.fontName)));
    font_->setCurrentIndex(font >= 0 ? font : 0);
    fontSize_->setValue(p.fontSize > 0 ? p.fontSize : 12);
    textColor_->setColor(propsQColor(p.textColor));
    align_->setCurrentIndex(std::clamp(p.align, 0, 2));
    for (QWidget* w : {static_cast<QWidget*>(font_), static_cast<QWidget*>(fontSize_),
                       static_cast<QWidget*>(textColor_), static_cast<QWidget*>(align_)}) {
        setRowVisible(w, freeText);
    }
    textSection_->setVisible(freeText);

    // Общие.
    author_->setText(QString::fromStdString(p.author));
    subject_->setText(QString::fromStdString(p.subject));
    type_->setText(typeTitle(p));
    const std::int64_t when = p.modified > 0 ? p.modified : p.created;
    modified_->setText(when > 0
                           ? QLocale().toString(QDateTime::fromSecsSinceEpoch(when), QLocale::ShortFormat)
                           : tr("нет сведений"));
    locked_->setChecked((p.flags & annot::kAnnotLocked) != 0);

    // Только чтение — только просмотр.
    const bool editable = view_.tools().editable();
    tabs_->widget(Appearance)->setEnabled(editable);
    author_->setReadOnly(!editable);
    subject_->setReadOnly(!editable);
    locked_->setEnabled(editable);
    makeDefault_->setEnabled(true);
    loaded_ = true;
    setEnabled(true);
    emit loadedChanged();
}

annot::AnnotPatch AnnotPropsDialog::patch() const {
    annot::AnnotPatch out;
    if (!loaded_) {
        return out;
    }
    const AnnotType t = props_.type;
    if (rowVisible(color_) && !propsSameColor(propsColor(color_->color()), props_.color)) {
        out.color = propsColor(color_->color());
    }
    if (rowVisible(fill_) && !propsSameColor(propsColor(fill_->color()), props_.interiorColor)) {
        out.interiorColor = propsColor(fill_->color());
    }
    if (rowVisible(width_) && std::fabs(width_->value() - props_.borderWidth) > 0.01) {
        out.borderWidth = static_cast<float>(width_->value());
    }
    if (rowVisible(lineStyle_)) {
        const auto style = static_cast<LineStyle>(lineStyle_->currentData().toInt());
        if (style != lineStyleOf(props_)) {
            out.dash = style == Dashed ? kPropsDashed : style == Dotted ? kPropsDotted : std::vector<float>{};
            out.borderStyle =
                style == Dashed || style == Dotted ? annot::BorderStyle::Dashed : annot::BorderStyle::Solid;
            if (propsClosedShape(t)) {
                out.cloudy = style == Cloud1 || style == Cloud2;
                if (*out.cloudy) {
                    out.cloudIntensity = style == Cloud2 ? 2.0f : 1.0f;
                }
            }
        }
    }
    if (std::fabs(opacity_->value() / 100.0 - props_.opacity) > 0.005) {
        out.opacity = static_cast<float>(opacity_->value() / 100.0);
    }
    if (rowVisible(icon_) && icon_->currentData().toString().toStdString() != props_.icon) {
        out.icon = icon_->currentData().toString().toStdString();
    }
    if (rowVisible(lineStart_) &&
        lineStart_->currentData().toInt() != static_cast<int>(props_.lineStartStyle)) {
        out.lineStartStyle = static_cast<annot::LineEnding>(lineStart_->currentData().toInt());
    }
    if (rowVisible(lineEnd_) && lineEnd_->currentData().toInt() != static_cast<int>(props_.lineEndStyle)) {
        out.lineEndStyle = static_cast<annot::LineEnding>(lineEnd_->currentData().toInt());
    }
    if (t == AnnotType::FreeText) {
        const std::string family = font_->currentData().toString().toStdString();
        if (family != annot::textFontFamily(props_.fontName)) {
            out.fontName = family;
        }
        if (std::fabs(fontSize_->value() - props_.fontSize) > 0.01) {
            out.fontSize = static_cast<float>(fontSize_->value());
        }
        if (textColor_->color().isValid() &&
            !propsSameColor(propsColor(textColor_->color()), props_.textColor)) {
            out.textColor = propsColor(textColor_->color());
        }
        if (align_->currentIndex() != props_.align) {
            out.align = align_->currentIndex();
        }
    }
    if (author_->text().toStdString() != props_.author) {
        out.author = author_->text().toStdString();
    }
    if (subject_->text().toStdString() != props_.subject) {
        out.subject = subject_->text().toStdString();
    }
    const bool wasLocked = (props_.flags & annot::kAnnotLocked) != 0;
    if (locked_->isChecked() != wasLocked) {
        out.flags = locked_->isChecked() ? (props_.flags | annot::kAnnotLocked)
                                         : (props_.flags & ~annot::kAnnotLocked);
    }
    return out;
}

annot::AnnotProps AnnotPropsDialog::edited() const {
    annot::AnnotProps p = props_;
    const annot::AnnotPatch d = patch();
    if (d.color) {
        p.color = *d.color;
    }
    if (d.interiorColor) {
        p.interiorColor = *d.interiorColor;
    }
    if (d.borderWidth) {
        p.borderWidth = *d.borderWidth;
    }
    if (d.opacity) {
        p.opacity = *d.opacity;
    }
    if (d.icon) {
        p.icon = *d.icon;
    }
    if (d.lineStartStyle) {
        p.lineStartStyle = *d.lineStartStyle;
    }
    if (d.lineEndStyle) {
        p.lineEndStyle = *d.lineEndStyle;
    }
    if (d.fontName) {
        p.fontName = *d.fontName;
    }
    if (d.fontSize) {
        p.fontSize = *d.fontSize;
    }
    if (d.textColor) {
        p.textColor = *d.textColor;
    }
    if (d.align) {
        p.align = *d.align;
    }
    return p;
}

void AnnotPropsDialog::accept() {
    if (loaded_ && session_ != nullptr) {
        if (makeDefault_->isChecked()) {
            saveAsDefault(edited());
        }
        annot::AnnotPatch p = patch();
        if (!p.empty() && view_.tools().editable()) {
            annot::update(*session_, ref_, std::move(p));  // ошибка — taskFailed сессии
        }
    }
    emit applied(ref_);
    QDialog::accept();
}

}  // namespace ot::ui
