// Модель полей формы AcroForm (план 6.1, п. 1; эталон 3.6; forms-01, forms-06, forms-31): обход /AcroForm
// /Fields (концевые поля с виджетами, полные имена «a.b.c»), типы, значения, флаги /Ff, варианты /Opt, /DA,
// сценарии /AA (K, F, V, C), действие кнопки /A, признаки XFA и NeedAppearances, порядок вычислений /CO и
// порядок табуляции /Tabs (R — по рядам, C — по столбцам, S и нет — порядок /Annots).
//
// Значения ставит MuPDF со своими событиями (pdf_set_text_field_value — keystroke и validate, при включённом
// JavaScript — сценарии поля; флажок и переключатель — pdf_set_field_value с состоянием «вкл.» или Off;
// список с мультивыбором — pdf_choice_widget_set_value), затем пересчёт (pdf_calculate_form — только с
// JavaScript) и внешний вид всех виджетов на страницах с полями — в той же операции журнала (иначе MuPDF
// синтезирует вид при рендере «неявной операцией»). NeedAppearances соблюдается (MuPDF 1.27+). Текстовое
// поле, в значении которого есть знаки вне WinAnsi (кириллица), получает /DA со встроенным TrueType
// (core/annot/freetext.h FieldFont) и свой внешний вид: синтез MuPDF рисует их невстроенным «HelvCYR»
// (эталон 3.6 «Граничные случаи»); поле пароля показывает звёздочки. Поле только для чтения (/Ff ReadOnly) и
// поле, закрытое подписью (/Lock подписанной подписи), не меняются.
//
// Указатели MuPDF наружу не выходят: поле — значения, виджет — страница и номер объекта. Поток — поток
// документа; правка — внутри Journal::Operation (вложенная присоединяется к операции вызывающего).
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/util/geometry.h"

namespace ot {
class Document;
}

namespace ot::forms {

enum class FieldType { Unknown, PushButton, CheckBox, RadioButton, Text, ComboBox, ListBox, Signature };
// "button", "checkbox", "radiobutton", "text", "combobox", "listbox", "signature", "unknown".
const char* fieldTypeName(FieldType type) noexcept;

// Флаги /Ff (ISO 32000-1, табл. 221, 226, 228, 230).
namespace ff {
inline constexpr int kReadOnly = 1;
inline constexpr int kRequired = 1 << 1;
inline constexpr int kNoExport = 1 << 2;
inline constexpr int kMultiline = 1 << 12;
inline constexpr int kPassword = 1 << 13;
inline constexpr int kNoToggleToOff = 1 << 14;
inline constexpr int kRadio = 1 << 15;
inline constexpr int kPushButton = 1 << 16;
inline constexpr int kCombo = 1 << 17;
inline constexpr int kEdit = 1 << 18;
inline constexpr int kSort = 1 << 19;
inline constexpr int kFileSelect = 1 << 20;
inline constexpr int kMultiSelect = 1 << 21;
inline constexpr int kDoNotSpellCheck = 1 << 22;
inline constexpr int kDoNotScroll = 1 << 23;
inline constexpr int kComb = 1 << 24;
inline constexpr int kRichText = 1 << 25;
inline constexpr int kRadiosInUnison = 1 << 25;
inline constexpr int kCommitOnSelChange = 1 << 26;
}  // namespace ff

// Формат текстового поля по сценарию /AA /F (функции AF* Acrobat).
enum class TextFormat { None, Number, Percent, Date, Time, Special };

enum class ActionKind {
    None,
    ResetForm,
    SubmitForm,
    ImportData,
    JavaScript,
    GoTo,
    GoToR,
    Uri,
    Named,
    Launch,
    Other
};
// "none", "ResetForm", "SubmitForm"… — имя /S.
const char* actionKindName(ActionKind kind) noexcept;

// Действие кнопки (/A виджета или поля; первое в цепочке /Next).
struct FieldAction {
    ActionKind kind = ActionKind::None;
    std::string target;               // URI, SubmitForm/ImportData /F, GoToR — файл, Named — /N
    int flags = 0;                    // /Flags (SubmitForm, ResetForm)
    std::vector<std::string> fields;  // /Fields — полные имена
    std::string script;               // JavaScript /JS
    int page = -1;                    // GoTo — страница назначения (с 0); −1 — неизвестна
    bool hasNext = false;             // есть /Next
};

struct ChoiceOption {
    std::string exportValue;  // первое значение пары /Opt (или единственное)
    std::string label;        // что показывать
};

struct Widget {
    int page = -1;
    int objnum = 0;
    RectF rect;               // координаты страницы MuPDF (как pdf_bound_widget)
    std::string onState;      // флажок, переключатель: имя состояния «вкл.» (/AP /N)
    bool on = false;          // /AS — не Off
    int rotation = 0;         // /MK /R
    bool hidden = false;      // /F Hidden или NoView
    std::string caption;      // /MK /CA кнопки
    std::string exportValue;  // флажок, переключатель: /Opt[i] этого виджета, иначе onState
};

// /DA поля: шрифт (имя ресурса /DR), кегль (0 — авто), цвет RGB 0…1, /Q (0 — влево, 1 — центр, 2 — вправо).
struct TextStyle {
    std::string font;
    float size = 0;
    std::array<float, 3> color{};
    int quadding = 0;
};

struct Field {
    std::string name;  // полное имя
    FieldType type = FieldType::Unknown;
    int flags = 0;
    std::string value;                  // /V текстом (флажок — имя состояния или «Off»)
    std::vector<std::string> values;    // список: выбранные значения; прочие — {value}, если оно есть
    std::string defaultValue;           // /DV
    std::string tooltip;                // /TU
    std::string mappingName;            // /TM
    int maxLen = 0;                     // /MaxLen
    std::vector<ChoiceOption> options;  // /Opt
    std::vector<Widget> widgets;
    TextFormat format = TextFormat::None;
    std::string formatScript;     // /AA /F
    std::string keystrokeScript;  // /AA /K
    std::string validateScript;   // /AA /V
    std::string calculateScript;  // /AA /C
    FieldAction action;           // кнопка
    TextStyle style;
    bool isSigned = false;  // подписанное поле подписи
    bool locked = false;    // поле закрыто подписанной подписью (/Lock) — правка запрещена

    bool readOnly() const noexcept { return (flags & ff::kReadOnly) != 0 || locked || isSigned; }
    bool required() const noexcept { return (flags & ff::kRequired) != 0; }
    bool multiline() const noexcept { return (flags & ff::kMultiline) != 0; }
    bool password() const noexcept { return (flags & ff::kPassword) != 0; }
    bool comb() const noexcept { return (flags & ff::kComb) != 0 && maxLen > 0; }
    bool editable() const noexcept { return type == FieldType::ComboBox && (flags & ff::kEdit) != 0; }
    bool multiSelect() const noexcept {
        return type == FieldType::ListBox && (flags & ff::kMultiSelect) != 0;
    }
    bool noExport() const noexcept { return (flags & ff::kNoExport) != 0; }
    // Значение флажка или переключателя «вкл.»: есть виджет с состоянием value (или экспортным значением).
    bool checked() const;
};

struct Form {
    std::vector<Field> fields;                  // концевые поля в порядке обхода /Fields
    std::vector<std::string> calculationOrder;  // /CO
    bool needAppearances = false;
    bool xfa = false;         // /AcroForm /XFA — «XFA не поддерживается, показана AcroForm-часть»
    bool javaScript = false;  // в документе есть сценарии (hasJavaScript)
    int sigFlags = 0;         // /SigFlags

    const Field* find(std::string_view name) const;
    bool empty() const noexcept { return fields.empty(); }
    // Поле, которому принадлежит виджет; nullptr — нет.
    const Field* fieldOf(int page, int objnum) const;
};

// Все поля документа; не PDF или нет формы — пусто.
Form readForm(const Document& doc);
std::optional<Field> readField(const Document& doc, std::string_view name);
// Виджеты страницы в порядке /Annots (с именем поля): для слоя заполнения.
struct PageWidget {
    Widget widget;
    std::string field;
};
std::vector<PageWidget> pageWidgets(const Document& doc, int page);
// В документе есть JavaScript: дерево имён /JavaScript, /OpenAction, /AA каталога и страниц, сценарии полей и
// действия JavaScript виджетов.
bool hasJavaScript(const Document& doc);

// Порядок табуляции страницы (эталон 3.6).
enum class TabOrder { Annots, Row, Column, Structure };
TabOrder tabOrder(const Document& doc, int page);
// Упорядочить виджеты (номер объекта и рамка в координатах страницы, в порядке /Annots): Row — ряды сверху
// вниз, в ряду слева направо (ряд — пересечение по высоте больше половины меньшей рамки); Column — столбцы
// слева направо, в столбце сверху вниз; Annots и Structure — как есть (структура — упрощённо по /Annots).
std::vector<int> orderWidgets(const std::vector<std::pair<int, RectF>>& widgets, TabOrder order);
// Порядок обхода Tab по странице: видимые виджеты полей, кроме только для чтения (у переключателей — первый
// виджет группы или выбранный).
std::vector<int> tabSequence(const Document& doc, int page);
// То же по прочитанной форме: порядок /Tabs страницы и номера объектов её /Annots (annotOrder).
std::vector<int> tabSequence(const Form& form, int page, TabOrder order, const std::vector<int>& annots);
std::vector<int> annotOrder(const Document& doc, int page);

// ---------------------------------------------------------------- правка

struct SetOptions {
    // Пересчитать форму после (pdf_calculate_form; без JavaScript вычислений нет).
    bool recalculate = true;
};

struct SetResult {
    bool accepted = false;   // false — отказ (проверка сценарием, только для чтения, нет такого варианта)
    std::string value;       // значение поля после (у отказа — прежнее)
    std::vector<int> pages;  // страницы, вид которых изменился (по возрастанию)
    // Причина отказа для журнала: "unknown-field", "read-only", "locked", "rejected", "not-an-option",
    // "not-a-value-field".
    std::string error;
};

// Значение поля по полному имени: текст и список (combo — и своё, если /Edit) — с keystroke и validate,
// флажок — экспортное значение или имя состояния (пусто, «Off» — снять), переключатель — так же для группы.
// Операция «Fill field» (вложенная — в операции вызывающего).
SetResult setValue(Document& doc, std::string_view name, std::string_view value,
                   const SetOptions& options = {});
// Список с мультивыбором: значения — экспортные (или подписи вариантов).
SetResult setValues(Document& doc, std::string_view name, const std::vector<std::string>& values,
                    const SetOptions& options = {});
// Щелчок по флажку или переключателю (pdf_toggle_widget): соседи группы и /V — как у Acrobat.
SetResult toggle(Document& doc, int page, int objnum, const SetOptions& options = {});
// «Очистить форму» и действие ResetForm (forms-06): /V ← /DV (нет /DV — значение снимается), флажки — по /DV;
// names пусто — все поля, exclude — все, кроме names. Операция «Reset form». Страницы с изменившимся видом.
std::vector<int> resetForm(Document& doc, const std::vector<std::string>& names = {}, bool exclude = false);
// Пересчитать форму (pdf_calculate_form; без JavaScript — ничего). Операция «Calculate form».
std::vector<int> recalculate(Document& doc);
// Значение для показа: формат /AA /F при включённом JavaScript (AFNumber_Format и др. из util.js MuPDF),
// иначе /V. Нет поля — пусто.
std::string formattedValue(Document& doc, std::string_view name);

// Нажатие кнопки (событие «отпущена»): цепочку /A с JavaScript (при включённом) и ResetForm выполняет MuPDF;
// SubmitForm, URI, GoTo, ImportData, Named — не ядра: action возвращается вызывающему.
struct ClickResult {
    FieldAction action;
    std::vector<int> pages;
};
ClickResult click(Document& doc, int page, int objnum);
// Фокус пришёл в поле / ушёл из него (/AA /Fo, /Bl — только с JavaScript). Страницы с изменившимся видом.
std::vector<int> focus(Document& doc, int page, int objnum, bool in);

}  // namespace ot::forms
