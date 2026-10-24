// QSS из токенов (эталон 6.4): только то, чего палитра Fusion не умеет — главная кнопка, подсказки, холст,
// компоненты src/ui/widgets (по классам ot--ui--…, варианты — динамическими свойствами). Единственное место
// QSS. Правила компонентов — одна таблица: из неё собирается QSS и пары «текст/фон» componentColors() для
// теста контраста (ui.theme, ui.widgets), так что проверяется ровно то, что рисуется.
#include <utility>

#include "ui/theme/theme.h"

namespace ot::ui {

namespace {

QString css(const QColor& c) {
    if (c.alpha() == 255) {
        return c.name(QColor::HexRgb);
    }
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

QString pxv(int v) { return QString::number(v) + QStringLiteral("px"); }

QColor primaryHover(const Palette& p) { return mix(p.accent.color(), p.text.color(), 0.14); }
QColor disabledText(const Palette& p) { return mix(p.surface.color(), p.textMuted.color(), 0.65); }
QColor pressed(const Palette& p) { return mix(p.hover.color(), p.text.color(), 0.10); }
QColor toneBorder(const Palette& p, const Rgba& tone) { return mix(p.surface.color(), tone.color(), 0.45); }

// Правило QSS и (если есть component) пара для проверки контраста. bg пустой — фон не задаётся (прозрачный
// или от Fusion), тогда контраст проверяется по under — фону, на котором компонент стоит.
struct Rule {
    QString selector;
    QString component;
    QString state;
    QColor fg;
    QColor bg;
    QColor under;
    QString extra;
};

std::vector<Rule> componentRules(const Palette& p, const Metrics& m) {
    const QColor S = p.surface.color();
    const QColor SA = p.surfaceAlt.color();
    const QColor T = p.text.color();
    const QColor TM = p.textMuted.color();
    const QColor A = p.accent.color();
    const QColor AS = p.accentSoft.color();
    const QColor OA = p.onAccent.color();
    const QColor H = p.hover.color();
    const QColor B = p.border.color();
    const QColor DT = disabledText(p);
    const QString r = pxv(m.radius);
    const QString rl = pxv(m.radiusLarge);
    const QString fr = pxv(m.focusRing);
    const QString field = QStringLiteral(
                              "border: 1px solid %1; border-radius: %2; padding: 0 %3; min-height: %4; "
                              "selection-background-color: %5; selection-color: %6;")
                              .arg(css(B), r, pxv(m.grid), pxv(m.row - m.grid * 2 - 2), css(A), css(OA));
    const QString fieldFocus =
        QStringLiteral("border: %1 solid %2; padding: 0 %3;").arg(fr, css(A), pxv(m.grid + 1 - m.focusRing));
    const QString focusBorder = QStringLiteral("border: %1 solid %2;").arg(fr, css(A));
    const QString none;
    std::vector<Rule> rules = {
        // ToolButton: прозрачная, наведение — hover, нажатая — accentSoft (иконка accent через QIcon::On),
        // фокус.
        {QStringLiteral("ot--ui--ToolButton"),
         QStringLiteral("ToolButton"),
         QStringLiteral("normal"),
         T,
         {},
         S,
         QStringLiteral(
             "background-color: transparent; border: 1px solid transparent; border-radius: %1; padding: %2;")
             .arg(r, pxv(m.grid))},
        {QStringLiteral("ot--ui--ToolButton:hover"),
         QStringLiteral("ToolButton"),
         QStringLiteral("hover"),
         T,
         H,
         {},
         none},
        {QStringLiteral("ot--ui--ToolButton:pressed"),
         QStringLiteral("ToolButton"),
         QStringLiteral("pressed"),
         T,
         pressed(p),
         {},
         none},
        {QStringLiteral("ot--ui--ToolButton:checked"),
         QStringLiteral("ToolButton"),
         QStringLiteral("checked"),
         T,
         AS,
         {},
         none},
        {QStringLiteral("ot--ui--ToolButton:focus, ot--ui--ToolButton[otFocus=\"true\"]"),
         {},
         {},
         {},
         {},
         {},
         QStringLiteral("border: %1 solid %2; padding: %3;").arg(fr, css(A), pxv(m.grid + 1 - m.focusRing))},
        {QStringLiteral("ot--ui--ToolButton:disabled"),
         QStringLiteral("ToolButton"),
         QStringLiteral("disabled"),
         DT,
         {},
         S,
         none},

        // SegmentedBar: подложка surfaceAlt, выбранный сегмент — surface с рамкой.
        {QStringLiteral("ot--ui--SegmentedBar"),
         QStringLiteral("SegmentedBar"),
         QStringLiteral("frame"),
         T,
         SA,
         {},
         QStringLiteral("border: 1px solid %1; border-radius: %2;").arg(css(B), r)},
        {QStringLiteral("ot--ui--SegmentedBar QToolButton"),
         QStringLiteral("SegmentedBar"),
         QStringLiteral("normal"),
         T,
         {},
         SA,
         QStringLiteral("background-color: transparent; border: 1px solid transparent; border-radius: %1; "
                        "padding: 0 %2;")
             .arg(pxv(std::max(1, m.radius - 1)), pxv(m.grid * 2))},
        {QStringLiteral("ot--ui--SegmentedBar QToolButton:hover"),
         QStringLiteral("SegmentedBar"),
         QStringLiteral("hover"),
         T,
         H,
         {},
         none},
        {QStringLiteral("ot--ui--SegmentedBar QToolButton:checked"),
         QStringLiteral("SegmentedBar"),
         QStringLiteral("checked"),
         T,
         S,
         {},
         QStringLiteral("border-color: %1;").arg(css(B))},
        {QStringLiteral(
             "ot--ui--SegmentedBar QToolButton:focus, ot--ui--SegmentedBar QToolButton[otFocus=\"true\"]"),
         {},
         {},
         {},
         {},
         {},
         focusBorder},
        {QStringLiteral("ot--ui--SegmentedBar QToolButton:disabled"),
         QStringLiteral("SegmentedBar"),
         QStringLiteral("disabled"),
         DT,
         {},
         SA,
         none},

        // Поля ввода: поиск, число, масштаб — одна рамка border, фокус — accent 2 px.
        {QStringLiteral("ot--ui--SearchField, ot--ui--NumberField, ot--ui--ZoomBox QComboBox"),
         QStringLiteral("SearchField"),
         QStringLiteral("normal"),
         T,
         S,
         {},
         field},
        {QStringLiteral(
             "ot--ui--SearchField:hover, ot--ui--NumberField:hover, ot--ui--ZoomBox QComboBox:hover"),
         {},
         {},
         {},
         {},
         {},
         QStringLiteral("border-color: %1;").arg(css(TM))},
        {QStringLiteral(
             "ot--ui--SearchField:focus, ot--ui--NumberField:focus, ot--ui--ZoomBox QComboBox:focus, "
             "ot--ui--SearchField[otFocus=\"true\"], ot--ui--NumberField[otFocus=\"true\"], "
             "ot--ui--ZoomBox[otFocus=\"true\"] QComboBox"),
         {},
         {},
         {},
         {},
         {},
         fieldFocus},
        {QStringLiteral("ot--ui--SearchField:disabled, ot--ui--NumberField:disabled, ot--ui--ZoomBox "
                        "QComboBox:disabled"),
         QStringLiteral("SearchField"),
         QStringLiteral("disabled"),
         DT,
         SA,
         {},
         none},
        {{}, QStringLiteral("SearchField"), QStringLiteral("selection"), OA, A, {}, none},
        // Стрелки Fusion под QSS-рамкой рисуются криво, а NoButtons под QSS сжимает поле ввода до 1 px:
        // кнопки нулевой ширины, шаг — клавишами ↑/↓, PgUp/PgDn и колесом в фокусе.
        {QStringLiteral("ot--ui--NumberField::up-button, ot--ui--NumberField::down-button"),
         {},
         {},
         {},
         {},
         {},
         QStringLiteral("width: 0px; border: none;")},
        {{}, QStringLiteral("NumberField"), QStringLiteral("normal"), T, S, {}, none},
        {{}, QStringLiteral("ZoomBox"), QStringLiteral("normal"), T, S, {}, none},

        // Заголовок панели.
        {QStringLiteral("ot--ui--PanelHeader"),
         QStringLiteral("PanelHeader"),
         QStringLiteral("normal"),
         T,
         S,
         {},
         QStringLiteral("border: none; border-bottom: 1px solid %1;").arg(css(B))},
        {QStringLiteral("ot--ui--PanelHeader #panelHeaderTitle"),
         QStringLiteral("PanelHeader"),
         QStringLiteral("title"),
         T,
         {},
         S,
         QStringLiteral("background-color: transparent;")},

        // Карточка: при наведении светлеет не фон, а рамка (подпись textMuted на hover светлой темы — 4,3:1).
        {QStringLiteral("ot--ui--Card"),
         QStringLiteral("Card"),
         QStringLiteral("normal"),
         T,
         S,
         {},
         QStringLiteral("border: 1px solid %1; border-radius: %2;").arg(css(B), rl)},
        {QStringLiteral("ot--ui--Card[clickable=\"true\"]:hover"),
         {},
         {},
         {},
         {},
         {},
         QStringLiteral("border-color: %1;").arg(css(TM))},
        {QStringLiteral(
             "ot--ui--Card[selected=\"true\"], ot--ui--Card:focus, ot--ui--Card[otFocus=\"true\"]"),
         {},
         {},
         {},
         {},
         {},
         focusBorder},
        {QStringLiteral("ot--ui--Card #cardTitle"),
         QStringLiteral("Card"),
         QStringLiteral("title"),
         T,
         {},
         S,
         QStringLiteral("background-color: transparent;")},
        {QStringLiteral("ot--ui--Card #cardText"),
         QStringLiteral("Card"),
         QStringLiteral("text"),
         TM,
         {},
         S,
         QStringLiteral("background-color: transparent;")},
        {QStringLiteral("ot--ui--Card #cardIcon"),
         {},
         {},
         {},
         {},
         {},
         QStringLiteral("background-color: transparent;")},
        {QStringLiteral("ot--ui--Card #cardTitle:disabled, ot--ui--Card #cardText:disabled"),
         QStringLiteral("Card"),
         QStringLiteral("disabled"),
         DT,
         {},
         S,
         none},

        // Чип: нейтральный — surfaceAlt; тоны — мягкий фон тона; фильтр — surface, выбранный — accentSoft.
        {QStringLiteral("ot--ui--Chip"),
         QStringLiteral("Chip"),
         QStringLiteral("neutral"),
         T,
         SA,
         {},
         QStringLiteral("border: 1px solid %1; border-radius: %2;")
             .arg(css(B), pxv((m.row - m.grid * 2) / 2))},
        {QStringLiteral("ot--ui--Chip QLabel"),
         {},
         {},
         T,
         {},
         {},
         QStringLiteral("background-color: transparent;")},
        {QStringLiteral("ot--ui--Chip[checkable=\"true\"]"),
         QStringLiteral("Chip"),
         QStringLiteral("filter"),
         T,
         S,
         {},
         none},
        {QStringLiteral("ot--ui--Chip[checkable=\"true\"]:hover"),
         QStringLiteral("Chip"),
         QStringLiteral("filter-hover"),
         T,
         H,
         {},
         none},
        {QStringLiteral("ot--ui--Chip[checked=\"true\"]"),
         QStringLiteral("Chip"),
         QStringLiteral("checked"),
         T,
         AS,
         {},
         QStringLiteral("border-color: %1;").arg(css(A))},
        {QStringLiteral("ot--ui--Chip:focus, ot--ui--Chip[otFocus=\"true\"]"),
         {},
         {},
         {},
         {},
         {},
         focusBorder},
        {QStringLiteral("ot--ui--Chip ot--ui--ToolButton#chipClose"),
         {},
         {},
         {},
         {},
         {},
         QStringLiteral("padding: 0px; border: none; border-radius: %1;").arg(pxv(m.iconRow / 2))},
        {QStringLiteral("ot--ui--Chip QLabel:disabled"),
         QStringLiteral("Chip"),
         QStringLiteral("disabled"),
         DT,
         {},
         SA,
         none},

        // Полоса сообщений и тост: мягкий фон тона, текст text; «Подробнее» — подчёркнутый text.
        {QStringLiteral("ot--ui--MessageBar QLabel, ot--ui--Toast QLabel"),
         {},
         {},
         T,
         {},
         {},
         QStringLiteral("background-color: transparent;")},
        {QStringLiteral("ot--ui--Toast #toastAction"),
         {},
         {},
         T,
         {},
         {},
         QStringLiteral(
             "background-color: transparent; border: none; text-decoration: underline; padding: 0 %1;")
             .arg(pxv(m.grid))},

        // Пустое состояние: строка — подпись.
        {QStringLiteral("ot--ui--EmptyState #emptyStateText"),
         QStringLiteral("EmptyState"),
         QStringLiteral("text"),
         TM,
         {},
         S,
         QStringLiteral("background-color: transparent;")},
        {{}, QStringLiteral("EmptyState"), QStringLiteral("panel"), TM, SA, {}, none},

        // Плавающая панель: surface, рамка, скругление 8 (тень — QGraphicsDropShadowEffect из токенов).
        {QStringLiteral("ot--ui--FloatingBar"),
         QStringLiteral("FloatingBar"),
         QStringLiteral("normal"),
         T,
         S,
         {},
         QStringLiteral("border: 1px solid %1; border-radius: %2;").arg(css(B), rl)},
        {QStringLiteral("ot--ui--FloatingBar #floatingSeparator"),
         {},
         {},
         {},
         B,
         {},
         QStringLiteral("border: none;")},

        // Лист свойств.
        {QStringLiteral("ot--ui--PropertySheet QLabel"),
         QStringLiteral("PropertySheet"),
         QStringLiteral("label"),
         T,
         {},
         S,
         QStringLiteral("background-color: transparent;")},
        {QStringLiteral("ot--ui--PropertySheet QLabel:disabled"),
         QStringLiteral("PropertySheet"),
         QStringLiteral("disabled"),
         DT,
         {},
         S,
         none},

        // Выбор цвета: как кнопка-иконка с рамкой.
        {QStringLiteral("ot--ui--ColorSwatch"),
         QStringLiteral("ColorSwatch"),
         QStringLiteral("normal"),
         T,
         {},
         S,
         QStringLiteral(
             "background-color: transparent; border: 1px solid %1; border-radius: %2; padding: %3;")
             .arg(css(B), r, pxv(m.grid / 2))},
        {QStringLiteral("ot--ui--ColorSwatch:hover"),
         QStringLiteral("ColorSwatch"),
         QStringLiteral("hover"),
         T,
         H,
         {},
         none},
        {QStringLiteral("ot--ui--ColorSwatch:focus, ot--ui--ColorSwatch[otFocus=\"true\"]"),
         {},
         {},
         {},
         {},
         {},
         focusBorder},
        {QStringLiteral("ot--ui--ColorSwatch::menu-indicator"),
         {},
         {},
         {},
         {},
         {},
         QStringLiteral("image: none; width: 0;")},

        // Оболочка (2.3): глобальная панель — surfaceAlt с нижней рамкой; вкладки документов — прозрачные,
        // наведение — hover, текущая — surface с рамкой; разделители панелей — border.
        {QStringLiteral("ot--ui--GlobalBar"),
         QStringLiteral("GlobalBar"),
         QStringLiteral("normal"),
         T,
         SA,
         {},
         QStringLiteral("border-bottom: 1px solid %1;").arg(css(B))},
        {QStringLiteral("ot--ui--GlobalBar #globalSeparator"), {}, {}, {}, B, {}, none},
        {QStringLiteral("ot--ui--DocumentTabs::tab"),
         QStringLiteral("DocumentTabs"),
         QStringLiteral("normal"),
         T,
         {},
         SA,
         QStringLiteral("background-color: transparent; border: 1px solid transparent; border-radius: %1; "
                        "padding: 0 %2; margin: 0 1px;")
             .arg(r, pxv(m.grid * 2))},
        {QStringLiteral("ot--ui--DocumentTabs::tab:hover"),
         QStringLiteral("DocumentTabs"),
         QStringLiteral("hover"),
         T,
         H,
         {},
         none},
        {QStringLiteral("ot--ui--DocumentTabs::tab:selected"),
         QStringLiteral("DocumentTabs"),
         QStringLiteral("selected"),
         T,
         S,
         {},
         QStringLiteral("border-color: %1;").arg(css(B))},
        {QStringLiteral("#shellBody::handle"), {}, {}, {}, B, {}, none},
        {QStringLiteral("ot--ui--ToolPanel"),
         QStringLiteral("ToolPanel"),
         QStringLiteral("normal"),
         T,
         S,
         {},
         QStringLiteral("border-right: 1px solid %1;").arg(css(B))},
        {QStringLiteral("ot--ui--ToolPanel #allToolsRows"), {}, {}, {}, S, {}, none},
        // Правая навигация (2.4): полоса 44 — surfaceAlt с рамкой слева, панель 280 — surface с рамкой слева;
        // ряд кнопок внизу страницы панели — рамка сверху.
        {QStringLiteral("ot--ui--NavPane"),
         QStringLiteral("NavPane"),
         QStringLiteral("normal"),
         T,
         SA,
         {},
         QStringLiteral("border-left: 1px solid %1;").arg(css(B))},
        {QStringLiteral("ot--ui--NavPanel"),
         QStringLiteral("NavPanel"),
         QStringLiteral("normal"),
         T,
         S,
         {},
         QStringLiteral("border-left: 1px solid %1;").arg(css(B))},
        // Строка состояния (2.4): surfaceAlt с рамкой сверху, разделители групп — border.
        {QStringLiteral("ot--ui--StatusBar"),
         QStringLiteral("StatusBar"),
         QStringLiteral("normal"),
         T,
         SA,
         {},
         QStringLiteral("border-top: 1px solid %1;").arg(css(B))},
        {QStringLiteral("ot--ui--StatusBar #statusSeparator"), {}, {}, {}, B, {}, none},
        {QStringLiteral("ot--ui--StatusBar #pageTotal"),
         QStringLiteral("StatusBar"),
         QStringLiteral("muted"),
         TM,
         {},
         SA,
         none},
        {QStringLiteral("ot--ui--NavPanel #paneButtons"),
         {},
         {},
         {},
         S,
         {},
         QStringLiteral("border-top: 1px solid %1;").arg(css(B))},
        // Главная (2.3): столбец разделов — surfaceAlt с рамкой справа, подписи — textMuted; «Краткие
        // сведения» —
        // surface с рамкой слева.
        {QStringLiteral("ot--ui--HomePage #homeNav"),
         QStringLiteral("HomeNav"),
         QStringLiteral("normal"),
         T,
         SA,
         {},
         QStringLiteral("border-right: 1px solid %1;").arg(css(B))},
        {QStringLiteral("ot--ui--HomePage #homeCaption"),
         QStringLiteral("HomeNav"),
         QStringLiteral("caption"),
         TM,
         {},
         SA,
         none},
        {QStringLiteral("ot--ui--HomePage #homeFolder"),
         QStringLiteral("HomePage"),
         QStringLiteral("folder"),
         TM,
         {},
         S,
         none},
        {QStringLiteral("ot--ui--BriefPane"),
         QStringLiteral("BriefPane"),
         QStringLiteral("normal"),
         T,
         S,
         {},
         QStringLiteral("border-left: 1px solid %1;").arg(css(B))},
        {QStringLiteral("ot--ui--BriefPane #briefCaption"),
         QStringLiteral("BriefPane"),
         QStringLiteral("caption"),
         TM,
         {},
         S,
         none},
        // «Редактировать PDF» (3.4): подписи разделов панели — textMuted 600, причина запрета правки — полоса
        // тона warn.
        {QStringLiteral("#editToolPanel QLabel#editSectionCaption"),
         QStringLiteral("EditPanel"),
         QStringLiteral("caption"),
         TM,
         {},
         S,
         QStringLiteral("font-weight: 600;")},
        {QStringLiteral("#editToolPanel QLabel#editLockNote"),
         QStringLiteral("EditPanel"),
         QStringLiteral("lock"),
         T,
         softBackground(p, p.warn),
         {},
         QStringLiteral("border: 1px solid %1; border-radius: %2; padding: %3;")
             .arg(css(toneBorder(p, p.warn)), r, pxv(m.grid * 2))},
        // «Использовать сертификат» (4.6): подписи разделов панели и причина запрета — как у «Редактировать
        // PDF»; подписи разделов диалогов подписей и Установок — QLabel[caption="true"].
        {QStringLiteral("#certificateToolPanel QLabel#certSectionCaption"),
         QStringLiteral("CertificatePanel"),
         QStringLiteral("caption"),
         TM,
         {},
         S,
         QStringLiteral("font-weight: 600;")},
        {QStringLiteral("#certificateToolPanel QLabel#certLockNote"),
         QStringLiteral("CertificatePanel"),
         QStringLiteral("lock"),
         T,
         softBackground(p, p.warn),
         {},
         QStringLiteral("border: 1px solid %1; border-radius: %2; padding: %3;")
             .arg(css(toneBorder(p, p.warn)), r, pxv(m.grid * 2))},
        {QStringLiteral("QLabel[caption=\"true\"]"),
         QStringLiteral("Caption"),
         QStringLiteral("normal"),
         TM,
         {},
         S,
         QStringLiteral("font-weight: 600;")},
    };
    // Тоны: полосы сообщений, тосты, чипы и строки-заметки диалогов (QLabel[note="…"], 4.6).
    const std::pair<const char*, Rgba> tones[] = {
        {"info", p.info}, {"warn", p.warn}, {"danger", p.danger},
        {"ok", p.ok},     {"form", p.form}, {"accent", p.accent},
    };
    for (const auto& [name, tone] : tones) {
        const QString t = QLatin1String(name);
        const QColor soft = softBackground(p, tone);
        const QString bar =
            QStringLiteral("border: 1px solid %1; border-radius: %2;").arg(css(toneBorder(p, tone)));
        if (t != QLatin1String("accent")) {
            const QString sel = QStringLiteral("ot--ui--MessageBar[tone=\"%1\"]").arg(t);
            rules.push_back({sel, QStringLiteral("MessageBar"), t, T, soft, {}, bar.arg(r)});
            rules.push_back({QStringLiteral("ot--ui--Toast[tone=\"%1\"]").arg(t),
                             QStringLiteral("Toast"),
                             t,
                             T,
                             soft,
                             {},
                             bar.arg(rl)});
            rules.push_back({QStringLiteral("QLabel[note=\"%1\"]").arg(t),
                             QStringLiteral("Note"),
                             t,
                             T,
                             soft,
                             {},
                             bar.arg(r) + QStringLiteral(" padding: %1;").arg(pxv(m.grid * 2))});
        }
        rules.push_back({QStringLiteral("ot--ui--Chip[tone=\"%1\"]").arg(t),
                         QStringLiteral("Chip"),
                         t,
                         T,
                         soft,
                         {},
                         QStringLiteral("border-color: %1;").arg(css(toneBorder(p, tone)))});
    }
    return rules;
}

// Шаблон базовых правил: @имя@ — цвет или размер из токенов.
const char* const kBaseTemplate = R"(
QToolTip {
    color: @surface@;
    background-color: @text@;
    border: none;
    padding: @grid@px @grid2@px;
}
QPushButton[primary="true"] {
    color: @onAccent@;
    background-color: @accent@;
    border: 1px solid @accent@;
    border-radius: @radius@px;
    padding: @grid@px @grid4@px;
    min-height: @row16@px;
}
QPushButton[primary="true"]:hover {
    background-color: @primaryHover@;
    border-color: @primaryHover@;
}
QPushButton[primary="true"]:focus {
    border: @focusRing@px solid @text@;
}
QPushButton[primary="true"]:disabled {
    color: @disabledText@;
    background-color: @hover@;
    border-color: @border@;
}
#pageView, #pageView > QWidget {
    background-color: @canvas@;
}
)";

}  // namespace

QColor softBackground(const Palette& p, const Rgba& tone) {
    return mix(p.surface.color(), tone.color(), 0.16);
}

RowColors rowColors(const Palette& p, RowState state) {
    switch (state) {
        case RowState::Hover:
            return {p.hover.color(), p.text.color(), p.text.color()};
        case RowState::Selected:
            return {p.accentSoft.color(), p.text.color(), p.text.color()};
        case RowState::Disabled:
            return {QColor(), disabledText(p), disabledText(p)};
        case RowState::Normal:
            break;
    }
    return {QColor(), p.text.color(), p.textMuted.color()};
}

QString makeStyleSheet(const Palette& p, const Metrics& m) {
    QString qss = QString::fromLatin1(kBaseTemplate);
    const std::pair<const char*, QString> vars[] = {
        {"@surface@", css(p.surface.color())},
        {"@canvas@", css(p.canvas.color())},
        {"@border@", css(p.border.color())},
        {"@text@", css(p.text.color())},
        {"@accent@", css(p.accent.color())},
        {"@onAccent@", css(p.onAccent.color())},
        {"@hover@", css(p.hover.color())},
        {"@primaryHover@", css(primaryHover(p))},
        {"@disabledText@", css(disabledText(p))},
        {"@grid@", QString::number(m.grid)},
        {"@grid2@", QString::number(m.grid * 2)},
        {"@grid4@", QString::number(m.grid * 4)},
        {"@row16@", QString::number(m.row - m.grid * 4)},
        {"@radius@", QString::number(m.radius)},
        {"@focusRing@", QString::number(m.focusRing)},
    };
    for (const auto& [key, value] : vars) {
        qss.replace(QLatin1String(key), value);
    }
    for (const Rule& rule : componentRules(p, m)) {
        if (rule.selector.isEmpty()) {
            continue;
        }
        QString body;
        if (rule.fg.isValid()) {
            body += QStringLiteral(" color: %1;").arg(css(rule.fg));
        }
        if (rule.bg.isValid()) {
            body += QStringLiteral(" background-color: %1;").arg(css(rule.bg));
        }
        if (!rule.extra.isEmpty()) {
            body += QLatin1Char(' ') + rule.extra;
        }
        qss += rule.selector + QStringLiteral(" {") + body + QStringLiteral(" }\n");
    }
    return qss;
}

std::vector<ColorPair> componentColors(const Palette& p) {
    const QColor text = p.text.color();
    std::vector<ColorPair> pairs = {
        {QStringLiteral("window"), QStringLiteral("normal"), text, p.surface.color()},
        {QStringLiteral("panel"), QStringLiteral("normal"), text, p.surfaceAlt.color()},
        {QStringLiteral("canvas"), QStringLiteral("normal"), text, p.canvas.color()},
        {QStringLiteral("caption"), QStringLiteral("normal"), p.textMuted.color(), p.surface.color()},
        {QStringLiteral("caption"), QStringLiteral("panel"), p.textMuted.color(), p.surfaceAlt.color()},
        {QStringLiteral("QPushButton"), QStringLiteral("normal"), text, p.surface.color()},
        {QStringLiteral("QPushButton.primary"), QStringLiteral("normal"), p.onAccent.color(),
         p.accent.color()},
        {QStringLiteral("QPushButton.primary"), QStringLiteral("hover"), p.onAccent.color(), primaryHover(p)},
        {QStringLiteral("tooltip"), QStringLiteral("normal"), p.surface.color(), text},
    };
    for (const Rule& rule : componentRules(p, Tokens::base())) {
        const QColor bg = rule.bg.isValid() ? rule.bg : rule.under;
        if (rule.component.isEmpty() || !rule.fg.isValid() || !bg.isValid() ||
            rule.state == QLatin1String("disabled")) {
            continue;  // выключенное WCAG не нормирует
        }
        pairs.push_back({rule.component, rule.state, rule.fg, bg});
    }
    for (const auto& [state, name] :
         {std::pair{RowState::Normal, "normal"}, std::pair{RowState::Hover, "hover"},
          std::pair{RowState::Selected, "selected"}}) {
        const RowColors c = rowColors(p, state);
        const QColor bg = c.background.isValid() ? c.background : p.surface.color();
        pairs.push_back({QStringLiteral("ListRow"), QLatin1String(name), c.text, bg});
        pairs.push_back(
            {QStringLiteral("ListRow"), QLatin1String(name) + QStringLiteral(".detail"), c.detail, bg});
    }
    return pairs;
}

}  // namespace ot::ui
