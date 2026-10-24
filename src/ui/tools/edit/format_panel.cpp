#include "ui/tools/edit/format_panel.h"

#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <algorithm>

#include "services/system_fonts_dwrite.h"
#include "ui/theme/theme.h"
#include "ui/tools/edit/edit_mode.h"
#include "ui/widgets/color_swatch.h"
#include "ui/widgets/number_field.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

namespace {

constexpr const char* kAlignIcons[] = {"align-left", "align-right", "align-center", "align-justify"};

// Системные семейства DirectWrite — один раз на процесс (их сотни).
const QStringList& systemFamilies() {
    static const QStringList families = [] {
        QStringList out;
        for (const std::string& f : ot::systemFonts().families()) {
            out << QString::fromStdString(f);
        }
        out.removeDuplicates();
        std::sort(out.begin(), out.end(),
                  [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
        return out;
    }();
    return families;
}

void addHeader(QComboBox* box, const QString& text) {
    box->addItem(text);
    auto* model = qobject_cast<QStandardItemModel*>(box->model());
    if (model != nullptr) {
        QStandardItem* item = model->item(box->count() - 1);
        item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
        QFont f = item->font();
        f.setBold(true);
        item->setFont(f);
    }
}

}  // namespace

FormatPanel::FormatPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("formatPanel"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(m.grid * 2);

    family_ = new QComboBox(this);
    family_->setObjectName(QStringLiteral("formatFamily"));
    family_->setMaxVisibleItems(20);
    column->addWidget(family_);
    connect(family_, &QComboBox::activated, this, [this](int index) {
        const QString name = family_->itemData(index).toString();
        if (!name.isEmpty()) {
            edit::StyleChange c;
            c.family = name.toStdString();
            apply(c);
        }
    });

    auto* row = new QHBoxLayout();
    row->setSpacing(m.grid);
    size_ = new NumberField(this);
    size_->setObjectName(QStringLiteral("formatSize"));
    size_->setRange(1, 999);
    size_->setDecimals(1);
    size_->setSingleStep(1);
    connect(size_, &NumberField::valueChanged, this, [this](double v) {
        edit::StyleChange c;
        c.size = static_cast<float>(v);
        apply(c);
    });
    row->addWidget(size_, 1);
    color_ = new ColorSwatch(QColor(Qt::black), this);
    color_->setObjectName(QStringLiteral("formatColor"));
    connect(color_, &ColorSwatch::colorChanged, this, [this](const QColor& color) {
        edit::StyleChange c;
        c.color = static_cast<std::uint32_t>(color.rgba());
        apply(c);
    });
    row->addWidget(color_);
    auto toggle = [this, row](const char* icon, const char* name) {
        auto* b = new ToolButton(QLatin1String(icon), QString(), this);
        b->setObjectName(QLatin1String(name));
        b->setCheckable(true);
        row->addWidget(b);
        return b;
    };
    bold_ = toggle("bold", "formatBold");
    italic_ = toggle("italic", "formatItalic");
    underline_ = toggle("underline", "formatUnderline");
    connect(bold_, &ToolButton::clicked, this, [this](bool on) {
        edit::StyleChange c;
        c.bold = on;
        apply(c);
    });
    connect(italic_, &ToolButton::clicked, this, [this](bool on) {
        edit::StyleChange c;
        c.italic = on;
        apply(c);
    });
    connect(underline_, &ToolButton::clicked, this, [this](bool on) {
        edit::StyleChange c;
        c.underline = on;
        apply(c);
    });
    column->addLayout(row);

    auto* alignRow = new QHBoxLayout();
    alignRow->setSpacing(m.grid);
    const edit::Align aligns[] = {edit::Align::Left, edit::Align::Center, edit::Align::Right,
                                  edit::Align::Justify};
    for (const edit::Align a : aligns) {
        auto* b = new ToolButton(QLatin1String(kAlignIcons[static_cast<int>(a)]), QString(), this);
        b->setObjectName(QStringLiteral("formatAlign_") + QLatin1String(edit::alignCode(a)));
        b->setCheckable(true);
        align_[static_cast<int>(a)] = b;
        alignRow->addWidget(b);
        connect(b, &ToolButton::clicked, this, [this, a] {
            if (mode_ != nullptr && !updating_) {
                mode_->setAlignment(a);
            }
            refresh();
        });
    }
    alignRow->addSpacing(m.grid * 2);
    super_ = new ToolButton(QStringLiteral("superscript"), QString(), this);
    super_->setObjectName(QStringLiteral("formatSuperscript"));
    super_->setCheckable(true);
    sub_ = new ToolButton(QStringLiteral("subscript"), QString(), this);
    sub_->setObjectName(QStringLiteral("formatSubscript"));
    sub_->setCheckable(true);
    alignRow->addWidget(super_);
    alignRow->addWidget(sub_);
    alignRow->addStretch(1);
    connect(super_, &ToolButton::clicked, this, [this](bool on) {
        if (mode_ != nullptr && !updating_) {
            mode_->setScript(on ? edit::Script::Super : edit::Script::Normal);
        }
        refresh();
    });
    connect(sub_, &ToolButton::clicked, this, [this](bool on) {
        if (mode_ != nullptr && !updating_) {
            mode_->setScript(on ? edit::Script::Sub : edit::Script::Normal);
        }
        refresh();
    });
    column->addLayout(alignRow);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(m.grid * 2);
    grid->setVerticalSpacing(m.grid);
    lineLabel_ = new QLabel(this);
    lineSpacing_ = new NumberField(this);
    lineSpacing_->setObjectName(QStringLiteral("formatLineSpacing"));
    lineSpacing_->setRange(0.5, 5.0);
    lineSpacing_->setDecimals(2);
    lineSpacing_->setSingleStep(0.1);
    lineLabel_->setBuddy(lineSpacing_);
    charLabel_ = new QLabel(this);
    charSpacing_ = new NumberField(this);
    charSpacing_->setObjectName(QStringLiteral("formatCharSpacing"));
    charSpacing_->setRange(-20, 100);
    charSpacing_->setDecimals(2);
    charSpacing_->setSingleStep(0.1);
    charLabel_->setBuddy(charSpacing_);
    grid->addWidget(lineLabel_, 0, 0);
    grid->addWidget(lineSpacing_, 0, 1);
    grid->addWidget(charLabel_, 1, 0);
    grid->addWidget(charSpacing_, 1, 1);
    grid->setColumnStretch(1, 1);
    column->addLayout(grid);
    connect(lineSpacing_, &NumberField::valueChanged, this, [this](double v) {
        if (mode_ != nullptr && !updating_) {
            mode_->setLineSpacing(static_cast<float>(v));
        }
    });
    connect(charSpacing_, &NumberField::valueChanged, this, [this](double v) {
        edit::StyleChange c;
        c.charSpacing = static_cast<float>(v);
        apply(c);
    });

    retranslate();
    rebuildFamilies(QString());
    refresh();
}

FormatPanel::~FormatPanel() = default;

ToolButton* FormatPanel::alignButton(edit::Align align) const { return align_[static_cast<int>(align)]; }

void FormatPanel::setMode(EditMode* mode) {
    if (mode_ == mode) {
        refresh();
        return;
    }
    QObject::disconnect(caretConnection_);
    QObject::disconnect(editingConnection_);
    mode_ = mode;
    if (mode != nullptr) {
        caretConnection_ = connect(mode, &EditMode::caretChanged, this, &FormatPanel::refresh);
        editingConnection_ = connect(mode, &EditMode::editingChanged, this, &FormatPanel::refresh);
    }
    refresh();
}

void FormatPanel::setDocumentFonts(const QStringList& families) {
    if (families == documentFonts_) {
        return;
    }
    documentFonts_ = families;
    rebuildFamilies(family_->currentData().toString());
}

void FormatPanel::setShowSystemFonts(bool on) {
    if (on == showSystem_) {
        return;
    }
    showSystem_ = on;
    rebuildFamilies(family_->currentData().toString());
}

void FormatPanel::rebuildFamilies(const QString& current) {
    const QSignalBlocker block(family_);
    family_->clear();
    QStringList doc = documentFonts_;
    if (!current.isEmpty() && !doc.contains(current, Qt::CaseInsensitive) &&
        !(showSystem_ && systemFamilies().contains(current, Qt::CaseInsensitive))) {
        doc.prepend(current);
    }
    if (!doc.isEmpty()) {
        addHeader(family_, tr("В документе"));
        for (const QString& f : doc) {
            family_->addItem(f, f);
        }
    }
    if (showSystem_) {
        if (family_->count() > 0) {
            family_->insertSeparator(family_->count());
        }
        addHeader(family_, tr("Системные"));
        for (const QString& f : systemFamilies()) {
            family_->addItem(f, f);
        }
    }
    const int index = family_->findData(current, Qt::UserRole, Qt::MatchFixedString);
    family_->setCurrentIndex(index);
}

void FormatPanel::apply(const edit::StyleChange& change) {
    if (updating_ || mode_ == nullptr) {
        return;
    }
    if (mode_->editing()) {
        mode_->applyStyle(change);
    } else {
        // Каретки нет — стиль следующего «Добавить текст».
        mode_->setNewTextStyle(change.applied(mode_->newTextStyle()));
    }
    refresh();
}

void FormatPanel::refresh() {
    updating_ = true;
    const bool editing = mode_ != nullptr && mode_->editing();
    const bool usable = mode_ != nullptr && !mode_->locked();
    edit::CharStyle style;
    if (editing) {
        style = mode_->currentStyle().value_or(edit::CharStyle{});
    } else if (mode_ != nullptr) {
        style = mode_->newTextStyle();
    }
    const QString family = EditMode::familyOf(style);
    if (family_->findData(family, Qt::UserRole, Qt::MatchFixedString) < 0) {
        rebuildFamilies(family);
    } else {
        const QSignalBlocker block(family_);
        family_->setCurrentIndex(family_->findData(family, Qt::UserRole, Qt::MatchFixedString));
    }
    {
        const QSignalBlocker b1(size_);
        size_->setValue(style.size);
    }
    color_->setColor(QColor::fromRgba(style.color));
    bold_->setChecked(style.bold);
    italic_->setChecked(style.italic);
    underline_->setChecked(style.underline);
    const edit::Script script = edit::scriptOf(style);
    super_->setChecked(script == edit::Script::Super);
    sub_->setChecked(script == edit::Script::Sub);
    {
        const QSignalBlocker b2(charSpacing_);
        charSpacing_->setValue(style.charSpacing);
    }
    const edit::ParagraphEditor* e = editing ? mode_->editor() : nullptr;
    for (int i = 0; i < 4; ++i) {
        align_[i]->setChecked(e != nullptr && static_cast<int>(e->alignment()) == i);
        align_[i]->setEnabled(editing && usable);
    }
    {
        const QSignalBlocker b3(lineSpacing_);
        lineSpacing_->setValue(e != nullptr ? e->lineSpacing() : edit::ParagraphEditor::kDefaultLineSpacing);
    }
    for (QWidget* w : std::initializer_list<QWidget*>{family_, size_, color_, bold_, italic_, underline_}) {
        w->setEnabled(usable);
    }
    for (QWidget* w :
         std::initializer_list<QWidget*>{super_, sub_, lineSpacing_, charSpacing_, lineLabel_, charLabel_}) {
        w->setEnabled(editing && usable);
    }
    updating_ = false;
}

void FormatPanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

void FormatPanel::retranslate() {
    family_->setAccessibleName(tr("Шрифт"));
    family_->setToolTip(tr("Шрифт"));
    size_->setUnit(tr("пт"));
    size_->setAccessibleName(tr("Кегль"));
    size_->setToolTip(tr("Кегль"));
    color_->setAccessibleName(tr("Цвет текста"));
    color_->setToolTip(tr("Цвет текста"));
    bold_->setLabel(tr("Полужирный (Ctrl+B)"));
    italic_->setLabel(tr("Курсив (Ctrl+I)"));
    underline_->setLabel(tr("Подчёркнутый (Ctrl+U)"));
    align_[static_cast<int>(edit::Align::Left)]->setLabel(tr("По левому краю"));
    align_[static_cast<int>(edit::Align::Center)]->setLabel(tr("По центру"));
    align_[static_cast<int>(edit::Align::Right)]->setLabel(tr("По правому краю"));
    align_[static_cast<int>(edit::Align::Justify)]->setLabel(tr("По ширине"));
    super_->setLabel(tr("Надстрочный"));
    sub_->setLabel(tr("Подстрочный"));
    lineLabel_->setText(tr("&Межстрочный:"));
    lineSpacing_->setAccessibleName(tr("Межстрочный интервал"));
    lineSpacing_->setToolTip(tr("Межстрочный интервал — доля кегля"));
    charLabel_->setText(tr("Меж&буквенный:"));
    charSpacing_->setUnit(tr("пт"));
    charSpacing_->setAccessibleName(tr("Межбуквенный интервал"));
}

}  // namespace ot::ui
