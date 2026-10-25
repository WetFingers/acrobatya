// Редакторы полей формы поверх PageView (план 6.1, п. 2; эталон 3.6; forms-01, forms-03, forms-15): виджет Qt
// в рамке виджета поля (пункты страницы → вьюпорт заново при прокрутке и масштабе), шрифт /DA (Helv — Arial,
// TiRo — Times New Roman, Cour — Courier New) кеглем поля с масштабом вида, цвет текста /DA:
//   текст — QLineEdit (пароль — точки, /MaxLen, гребёнка — знак в ячейке межбуквенным интервалом);
//   многострочный — QPlainTextEdit (Ctrl+Enter — записать);
//   дата по AFDate_FormatEx — поле с кнопкой календаря (QCalendarWidget, формат даты — из сценария формата);
//   combo — QComboBox (редактируемый /Edit — своё значение), список — QListWidget (мультивыбор — Ctrl/Shift).
// Фиксация — Enter, Tab/Shift+Tab (со сдвигом фокуса), потеря фокуса (не в своё всплывающее окно); Esc —
// отмена. Сам редактор документ не правит: commitRequested — владелец (FormFillTool) пишет значение задачей.
#pragma once

#include <QDate>
#include <QFrame>
#include <QPointer>
#include <QString>
#include <QStringList>

#include "core/forms/fields.h"

class QCalendarWidget;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QToolButton;

namespace ot::ui {

class PageView;

// Формат даты Acrobat (AFDate_FormatEx: d, dd, m, mm, mmm, mmmm, yy, yyyy, H, HH, h, hh, MM, ss, tt) → формат
// QDate/QDateTime; пусто — не дата.
QString qtDateFormat(const QString& acrobat);
// Формат даты из сценария /AA /F (AFDate_FormatEx("…") или AFDate_Format(n)); пусто — не дата.
QString dateFormatOf(const forms::Field& field);

class FieldEditor : public QFrame {
    Q_OBJECT
public:
    enum class Kind { Line, Multiline, Date, Combo, List };

    // Редактор поля field для виджета widget (страница и рамка) со значением value.
    FieldEditor(PageView& view, const forms::Field& field, const forms::Widget& widget, const QString& value);
    ~FieldEditor() override;

    Kind kind() const noexcept { return kind_; }
    const QString& fieldName() const noexcept { return name_; }
    int page() const noexcept { return page_; }
    int objnum() const noexcept { return objnum_; }
    // Что записать: текст (Line, Multiline, Date, Combo) или выбранные значения (List).
    QString text() const;
    QStringList selected() const;
    bool changed() const;
    // Поле ввода (тесты): QLineEdit, QPlainTextEdit, QComboBox или QListWidget.
    QWidget* input() const noexcept { return input_; }
    QToolButton* calendarButton() const noexcept { return calendarButton_; }
    // Положение и шрифт по виду (прокрутка, масштаб).
    void place();
    // Combo: открыть список; дата — календарь.
    void openPopup();
    // Дата календаря (тесты; как выбор щелчком).
    void pickDate(const QDate& date);
    // Записано или брошено — больше не фиксировать.
    void finish() noexcept { finished_ = true; }
    bool finished() const noexcept { return finished_; }

signals:
    // move: 0 — записать и закрыть, +1 / −1 — записать и перейти к следующему / предыдущему полю (Tab).
    void commitRequested(int move);
    void cancelRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void build(const forms::Field& field, const QString& value);
    void applyFont();

    PageView& view_;
    Kind kind_ = Kind::Line;
    QString name_;
    int page_ = -1;
    int objnum_ = 0;
    RectF rect_;
    forms::TextStyle style_;
    int maxLen_ = 0;
    bool comb_ = false;
    QString dateFormat_;
    QString original_;
    QStringList originalSelected_;
    QWidget* input_ = nullptr;
    QLineEdit* line_ = nullptr;
    QPlainTextEdit* multi_ = nullptr;
    QComboBox* combo_ = nullptr;
    QListWidget* list_ = nullptr;
    QToolButton* calendarButton_ = nullptr;
    QPointer<QFrame> calendarPopup_;
    QCalendarWidget* calendar_ = nullptr;
    bool finished_ = false;
};

}  // namespace ot::ui
