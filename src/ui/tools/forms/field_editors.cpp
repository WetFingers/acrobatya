#include "ui/tools/forms/field_editors.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCalendarWidget>
#include <QComboBox>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "core/annot/freetext.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/theme/tokens.h"
#include "ui/view/page_view.h"

namespace ot::ui {

QString qtDateFormat(const QString& acrobat) {
    QString out;
    QString literal;
    const auto flushLiteral = [&] {
        if (!literal.isEmpty()) {
            out += QLatin1Char('\'') + literal + QLatin1Char('\'');
            literal.clear();
        }
    };
    int i = 0;
    while (i < acrobat.size()) {
        const QChar c = acrobat.at(i);
        int n = 1;
        while (i + n < acrobat.size() && acrobat.at(i + n) == c) {
            ++n;
        }
        QString token;
        switch (c.unicode()) {
            case 'd':
                token = QString(std::min(n, 4), QLatin1Char('d'));
                break;
            case 'm':
                token = QString(std::min(n, 4), QLatin1Char('M'));  // месяц
                break;
            case 'M':
                token = QString(std::min(n, 2), QLatin1Char('m'));  // минуты
                break;
            case 'y':
                token = n >= 3 ? QStringLiteral("yyyy") : QStringLiteral("yy");
                break;
            case 'H':
                token = QString(std::min(n, 2), QLatin1Char('H'));
                break;
            case 'h':
                token = QString(std::min(n, 2), QLatin1Char('h'));
                break;
            case 's':
                token = QString(std::min(n, 2), QLatin1Char('s'));
                break;
            case 't':
                token = n >= 2 ? QStringLiteral("AP") : QStringLiteral("A");
                break;
            default:
                literal += QString(n, c);
                i += n;
                continue;
        }
        flushLiteral();
        out += token;
        i += n;
    }
    flushLiteral();
    return out;
}

QString dateFormatOf(const forms::Field& field) {
    if (field.format != forms::TextFormat::Date) {
        return {};
    }
    const QString script = QString::fromStdString(field.formatScript);
    static const QRegularExpression ex(QStringLiteral(R"(AFDate_FormatEx\s*\(\s*["']([^"']*)["'])"));
    if (const QRegularExpressionMatch m = ex.match(script); m.hasMatch()) {
        return qtDateFormat(m.captured(1));
    }
    // AFDate_Format(n) — список форматов Acrobat.
    static const char* const kFormats[] = {
        "m/d",      "m/d/yy", "mm/dd/yy", "mm/yy",       "d-mmm",        "d-mmm-yy",       "dd-mmm-yy",
        "yy-mm-dd", "mmm-yy", "mmmm-yy",  "mmm d, yyyy", "mmmm d, yyyy", "m/d/yy h:MM tt", "m/d/yy HH:MM"};
    static const QRegularExpression index(QStringLiteral(R"(AFDate_Format\s*\(\s*(\d+))"));
    if (const QRegularExpressionMatch m = index.match(script); m.hasMatch()) {
        const int n = m.captured(1).toInt();
        if (n >= 0 && n < static_cast<int>(std::size(kFormats))) {
            return qtDateFormat(QString::fromLatin1(kFormats[n]));
        }
    }
    return {};
}

FieldEditor::FieldEditor(PageView& view, const forms::Field& field, const forms::Widget& widget,
                         const QString& value)
    : QFrame(view.viewport()),
      view_(view),
      name_(QString::fromStdString(field.name)),
      page_(widget.page),
      objnum_(widget.objnum),
      rect_(widget.rect),
      style_(field.style),
      maxLen_(field.maxLen),
      comb_(field.comb() && !field.password()) {
    setObjectName(QStringLiteral("fieldEditor"));
    setAccessibleName(tr("Поле формы «%1»")
                          .arg(QString::fromStdString(field.tooltip.empty() ? field.name : field.tooltip)));
    setFrameShape(QFrame::NoFrame);
    build(field, value);
    connect(view_.horizontalScrollBar(), &QScrollBar::valueChanged, this, &FieldEditor::place);
    connect(view_.verticalScrollBar(), &QScrollBar::valueChanged, this, &FieldEditor::place);
    connect(&view_, &PageView::zoomChanged, this, &FieldEditor::place);
    connect(&view_, &PageView::layoutChanged, this, &FieldEditor::place);
    place();
    show();
    raise();
    input_->setFocus(Qt::MouseFocusReason);
}

FieldEditor::~FieldEditor() {
    if (calendarPopup_ != nullptr) {
        calendarPopup_->deleteLater();
    }
}

void FieldEditor::build(const forms::Field& field, const QString& value) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    original_ = value;
    dateFormat_ = dateFormatOf(field);
    const Qt::Alignment align = field.style.quadding == 1   ? Qt::AlignHCenter
                                : field.style.quadding == 2 ? Qt::AlignRight
                                                            : Qt::AlignLeft;
    if (field.type == forms::FieldType::ComboBox) {
        kind_ = Kind::Combo;
        combo_ = new QComboBox(this);
        combo_->setObjectName(QStringLiteral("fieldCombo"));
        combo_->setEditable(field.editable());
        int current = -1;
        for (std::size_t i = 0; i < field.options.size(); ++i) {
            const forms::ChoiceOption& o = field.options[i];
            combo_->addItem(QString::fromStdString(o.label), QString::fromStdString(o.exportValue));
            if (QString::fromStdString(o.exportValue) == value || QString::fromStdString(o.label) == value) {
                current = static_cast<int>(i);
            }
        }
        combo_->setCurrentIndex(current);
        if (current < 0 && field.editable()) {
            combo_->setEditText(value);
        }
        if (combo_->lineEdit() != nullptr) {
            combo_->lineEdit()->installEventFilter(this);
        }
        combo_->view()->installEventFilter(this);
        connect(combo_, qOverload<int>(&QComboBox::activated), this, [this] {
            if (!finished_) {
                emit commitRequested(0);
            }
        });
        input_ = combo_;
    } else if (field.type == forms::FieldType::ListBox) {
        kind_ = Kind::List;
        list_ = new QListWidget(this);
        list_->setObjectName(QStringLiteral("fieldList"));
        list_->setSelectionMode(field.multiSelect() ? QAbstractItemView::ExtendedSelection
                                                    : QAbstractItemView::SingleSelection);
        for (const forms::ChoiceOption& o : field.options) {
            auto* item = new QListWidgetItem(QString::fromStdString(o.label), list_);
            item->setData(Qt::UserRole, QString::fromStdString(o.exportValue));
            const bool chosen =
                std::find(field.values.begin(), field.values.end(), o.exportValue) != field.values.end();
            item->setSelected(chosen);
            if (chosen) {
                originalSelected_ << QString::fromStdString(o.exportValue);
            }
        }
        if ((field.flags & forms::ff::kCommitOnSelChange) != 0) {
            connect(list_, &QListWidget::itemSelectionChanged, this, [this] {
                if (!finished_) {
                    emit commitRequested(0);
                }
            });
        }
        input_ = list_;
    } else if (field.multiline()) {
        kind_ = Kind::Multiline;
        multi_ = new QPlainTextEdit(this);
        multi_->setObjectName(QStringLiteral("fieldMultiline"));
        multi_->setPlainText(value);
        multi_->setFrameShape(QFrame::NoFrame);
        input_ = multi_;
    } else {
        kind_ = dateFormat_.isEmpty() ? Kind::Line : Kind::Date;
        line_ = new QLineEdit(this);
        line_->setObjectName(QStringLiteral("fieldLine"));
        line_->setFrame(false);
        line_->setText(value);
        line_->setAlignment(align | Qt::AlignVCenter);
        if (field.password()) {
            line_->setEchoMode(QLineEdit::Password);
        }
        if (field.maxLen > 0) {
            line_->setMaxLength(field.maxLen);
        }
        input_ = line_;
    }
    input_->installEventFilter(this);
    layout->addWidget(input_, 1);
    if (kind_ == Kind::Date) {
        calendarButton_ = new QToolButton(this);
        calendarButton_->setObjectName(QStringLiteral("fieldCalendar"));
        calendarButton_->setIcon(Icons::icon(QStringLiteral("calendar")));
        calendarButton_->setToolTip(tr("Выбрать дату"));
        calendarButton_->setAccessibleName(tr("Выбрать дату"));
        calendarButton_->setFocusPolicy(Qt::NoFocus);
        connect(calendarButton_, &QToolButton::clicked, this, &FieldEditor::openPopup);
        layout->addWidget(calendarButton_);
    }
}

QString FieldEditor::text() const {
    switch (kind_) {
        case Kind::Combo: {
            if (combo_->isEditable()) {
                const QString typed = combo_->currentText();
                const int i = combo_->findText(typed);
                return i >= 0 ? combo_->itemData(i).toString() : typed;
            }
            return combo_->currentIndex() >= 0 ? combo_->currentData().toString() : QString();
        }
        case Kind::List:
            return selected().value(0);
        case Kind::Multiline:
            return multi_->toPlainText();
        case Kind::Line:
        case Kind::Date:
            break;
    }
    return line_->text();
}

QStringList FieldEditor::selected() const {
    QStringList out;
    if (list_ == nullptr) {
        return {text()};
    }
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->isSelected()) {
            out << list_->item(i)->data(Qt::UserRole).toString();
        }
    }
    return out;
}

bool FieldEditor::changed() const {
    if (kind_ == Kind::List) {
        return selected() != originalSelected_;
    }
    return text() != original_;
}

void FieldEditor::place() {
    if (finished_) {
        return;
    }
    const QTransform t = view_.pageTransform(page_);
    const QRectF box = t.mapRect(QRectF(QPointF(rect_.x0, rect_.y0), QPointF(rect_.x1, rect_.y1)));
    QRect r = box.toAlignedRect();
    if (kind_ == Kind::Combo) {
        r.setHeight(std::max(r.height(), 20));
    }
    if (calendarButton_ != nullptr) {
        r.setWidth(r.width() + r.height());  // кнопка календаря — справа от поля, квадрат его высоты
        calendarButton_->setFixedSize(r.height(), r.height());
    }
    setGeometry(r);
    applyFont();
}

void FieldEditor::applyFont() {
    const QTransform t = view_.pageTransform(page_);
    const double scale = std::max(0.01, std::hypot(t.m11(), t.m12()));
    const double height = (rect_.y1 - rect_.y0) * scale;
    double px = style_.size > 0 ? style_.size * scale : 0.0;
    if (px <= 0) {
        px = kind_ == Kind::Multiline || kind_ == Kind::List ? 12 * scale : height * 0.6;
    }
    QFont font(QString::fromStdString(annot::textFontFamily(style_.font)));
    font.setPixelSize(std::max(4, static_cast<int>(std::lround(px))));
    if (comb_ && maxLen_ > 0 && line_ != nullptr) {
        // Гребёнка: знак в ячейке — межбуквенный интервал в шаг ячейки, первый знак — по центру первой.
        const double cell = (rect_.x1 - rect_.x0) * scale / maxLen_;
        const double advance = QFontMetricsF(font).horizontalAdvance(QLatin1Char('0'));
        font.setLetterSpacing(QFont::AbsoluteSpacing, std::max(0.0, cell - advance));
        const int inset = std::max(0, static_cast<int>(std::lround((cell - advance) / 2)) - 2);
        line_->setTextMargins(inset, 0, 0, 0);
        line_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
    input_->setFont(font);
    const QColor color = QColor::fromRgbF(style_.color[0], style_.color[1], style_.color[2]);
    // Поле — на бумаге документа (она светлая в любой теме): фон редактора — подсветка полей светлой палитры.
    const QColor accent = Theme::colors().accent.color();
    setStyleSheet(
        QStringLiteral("QFrame#fieldEditor { background: %1; border: 1px solid %2; }"
                       "QLineEdit, QPlainTextEdit, QListWidget { background: transparent; color: %3; "
                       "border: none; }")
            .arg(kLightPalette.formField.color().name(QColor::HexRgb), accent.name(QColor::HexRgb),
                 color.name(QColor::HexRgb)));
}

void FieldEditor::openPopup() {
    if (combo_ != nullptr) {
        combo_->showPopup();
        return;
    }
    if (kind_ != Kind::Date) {
        return;
    }
    if (calendarPopup_ == nullptr) {
        calendarPopup_ = new QFrame(this, Qt::Popup);
        calendarPopup_->setObjectName(QStringLiteral("fieldCalendarPopup"));
        calendarPopup_->setFrameShape(QFrame::StyledPanel);
        auto* l = new QVBoxLayout(calendarPopup_);
        l->setContentsMargins(4, 4, 4, 4);
        calendar_ = new QCalendarWidget(calendarPopup_);
        calendar_->setObjectName(QStringLiteral("fieldCalendarWidget"));
        calendar_->setGridVisible(false);
        l->addWidget(calendar_);
        connect(calendar_, &QCalendarWidget::clicked, this, &FieldEditor::pickDate);
        connect(calendar_, &QCalendarWidget::activated, this, &FieldEditor::pickDate);
    }
    const QDate current = QDate::fromString(line_->text(), dateFormat_);
    calendar_->setSelectedDate(current.isValid() ? current : QDate::currentDate());
    calendarPopup_->adjustSize();
    calendarPopup_->move(mapToGlobal(QPoint(0, height())));
    calendarPopup_->show();
}

void FieldEditor::pickDate(const QDate& date) {
    if (line_ == nullptr || dateFormat_.isEmpty()) {
        return;
    }
    line_->setText(date.toString(dateFormat_));
    if (calendarPopup_ != nullptr) {
        calendarPopup_->hide();
    }
    emit commitRequested(0);
}

bool FieldEditor::eventFilter(QObject* watched, QEvent* event) {
    if (finished_) {
        return QFrame::eventFilter(watched, event);
    }
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        const Qt::KeyboardModifiers mods = key->modifiers() & ~Qt::KeypadModifier;
        if (key->key() == Qt::Key_Escape) {
            emit cancelRequested();
            return true;
        }
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
            emit commitRequested(key->key() == Qt::Key_Backtab || (mods & Qt::ShiftModifier) ? -1 : 1);
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (kind_ == Kind::Multiline && !(mods & Qt::ControlModifier)) {
                return false;  // перевод строки
            }
            if (kind_ == Kind::Combo && watched == combo_->view()) {
                return false;  // выбор строки списка — activated
            }
            emit commitRequested(0);
            return true;
        }
        if (kind_ == Kind::Date && key->key() == Qt::Key_Down && (mods & Qt::AltModifier)) {
            openPopup();
            return true;
        }
    }
    if (event->type() == QEvent::ShortcutOverride) {
        // Клавиши окна (Ctrl+Z, Del, однобуквенные) — полю, пока в нём набирают.
        event->accept();
        return false;
    }
    if (event->type() == QEvent::FocusOut) {
        // Решить после того, как фокус дошёл до нового места: свой список combo, календарь, кнопка — не уход.
        QTimer::singleShot(0, this, [this] {
            if (finished_) {
                return;
            }
            for (QWidget* w = QApplication::focusWidget(); w != nullptr; w = w->parentWidget()) {
                if (w == this) {
                    return;
                }
            }
            const bool popup = (combo_ != nullptr && combo_->view()->isVisible()) ||
                               (calendarPopup_ != nullptr && calendarPopup_->isVisible()) ||
                               QApplication::activePopupWidget() != nullptr;
            if (!popup) {
                emit commitRequested(0);
            }
        });
    }
    return QFrame::eventFilter(watched, event);
}

}  // namespace ot::ui
