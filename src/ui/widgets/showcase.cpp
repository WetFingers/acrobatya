#include "ui/widgets/showcase.h"

#include <QApplication>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

#include "ui/theme/theme.h"
#include "ui/widgets/card.h"
#include "ui/widgets/chip.h"
#include "ui/widgets/color_swatch.h"
#include "ui/widgets/empty_state.h"
#include "ui/widgets/floating_bar.h"
#include "ui/widgets/list_row.h"
#include "ui/widgets/message_bar.h"
#include "ui/widgets/number_field.h"
#include "ui/widgets/panel_header.h"
#include "ui/widgets/property_sheet.h"
#include "ui/widgets/search_field.h"
#include "ui/widgets/segmented_bar.h"
#include "ui/widgets/toast.h"
#include "ui/widgets/tone.h"
#include "ui/widgets/tool_button.h"
#include "ui/widgets/zoom_box.h"

namespace ot::ui {

namespace {

// Сюда смотрит состояние составного компонента (наведение и фокус — на внутренний элемент).
QWidget* stateTarget(QWidget* w) {
    if (auto* bar = qobject_cast<SegmentedBar*>(w)) {
        return bar->segment(1);
    }
    if (auto* zoom = qobject_cast<ZoomBox*>(w)) {
        return zoom->combo();
    }
    return w;
}

QLabel* caption(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("showcaseCaption"));
    return l;
}

using Factory = std::function<QWidget*(QWidget*)>;

// Таблица «компонент × состояние».
QWidget* stateTable(const std::vector<std::pair<QString, Factory>>& rows, QWidget* parent) {
    auto* box = new QWidget(parent);
    auto* grid = new QGridLayout(box);
    grid->setContentsMargins(0, 0, 0, 0);
    const Metrics& m = Theme::sizes();
    grid->setHorizontalSpacing(m.grid * 3);
    grid->setVerticalSpacing(m.grid * 2);
    const QString states[] = {QApplication::translate("ot::ui::Showcase", "Обычное"),
                              QApplication::translate("ot::ui::Showcase", "Наведение"),
                              QApplication::translate("ot::ui::Showcase", "Фокус"),
                              QApplication::translate("ot::ui::Showcase", "Выключено")};
    for (int c = 0; c < 4; ++c) {
        grid->addWidget(caption(states[c], box), 0, c + 1);
    }
    int r = 1;
    for (const auto& [name, make] : rows) {
        grid->addWidget(caption(name, box), r, 0);
        for (int c = 0; c < 4; ++c) {
            QWidget* w = make(box);
            forceShowcaseState(w, static_cast<ShowcaseState>(c));
            grid->addWidget(w, r, c + 1, Qt::AlignLeft | Qt::AlignVCenter);
        }
        ++r;
    }
    grid->setColumnStretch(5, 1);
    return box;
}

QWidget* buildShowcase(QWidget* parent) {
    auto* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("showcase"));
    page->setAutoFillBackground(true);
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(m.grid * 4, m.grid * 4, m.grid * 4, m.grid * 4);
    column->setSpacing(m.grid * 4);

    auto* head = new QHBoxLayout();
    auto* title = new QLabel(QApplication::translate("ot::ui::Showcase", "Витрина компонентов"), page);
    title->setObjectName(QStringLiteral("showcaseTitle"));
    title->setFont(homeTitleFont(m));
    head->addWidget(title);
    const ThemeKind kind = Theme::instance().kind();
    const QString kindText = kind == ThemeKind::Dark ? QApplication::translate("ot::ui::Showcase", "Тёмная")
                             : kind == ThemeKind::HighContrast
                                 ? QApplication::translate("ot::ui::Showcase", "Высокая контрастность")
                                 : QApplication::translate("ot::ui::Showcase", "Светлая");
    auto* themeChip = new Chip(kindText, Tone::Accent, page);
    head->addWidget(themeChip);
    if (Theme::instance().scale() > 1.0) {
        head->addWidget(
            new Chip(QApplication::translate("ot::ui::Showcase", "Крупные элементы"), Tone::Info, page));
    }
    head->addStretch(1);
    column->addLayout(head);

    // Элементы управления × состояния.
    const std::vector<std::pair<QString, Factory>> controls = {
        {QApplication::translate("ot::ui::Showcase", "Кнопка-иконка"),
         [](QWidget* p) {
             return new ToolButton(QStringLiteral("printer"),
                                   QApplication::translate("ot::ui::Showcase", "Печать"), p);
         }},
        {QApplication::translate("ot::ui::Showcase", "Нажатая"),
         [](QWidget* p) {
             auto* b = new ToolButton(QStringLiteral("highlighter"),
                                      QApplication::translate("ot::ui::Showcase", "Выделить текст"), p);
             b->setCheckable(true);
             b->setChecked(true);
             b->setLabelVisible(true);
             return b;
         }},
        {QApplication::translate("ot::ui::Showcase", "Сегменты"),
         [](QWidget* p) {
             auto* s = new SegmentedBar(p);
             s->addSegment(QApplication::translate("ot::ui::Showcase", "Одна"));
             s->addSegment(QApplication::translate("ot::ui::Showcase", "Непрерывно"));
             s->addSegment(QApplication::translate("ot::ui::Showcase", "Разворот"));
             return s;
         }},
        {QApplication::translate("ot::ui::Showcase", "Поиск"),
         [](QWidget* p) {
             auto* f = new SearchField(p);
             f->setText(QApplication::translate("ot::ui::Showcase", "договор"));
             f->setFixedWidth(Theme::sizes().grid * 40);
             return f;
         }},
        {QApplication::translate("ot::ui::Showcase", "Число"),
         [](QWidget* p) {
             auto* n = new NumberField(p);
             n->setRange(1, 1000);
             n->setDecimals(1);
             n->setUnit(QApplication::translate("ot::ui::Showcase", "пт"));
             n->setValue(12.5);
             n->setFixedWidth(Theme::sizes().grid * 24);
             return n;
         }},
        {QApplication::translate("ot::ui::Showcase", "Масштаб"),
         [](QWidget* p) {
             auto* z = new ZoomBox(p);
             z->setZoomPercent(125);
             return z;
         }},
        {QApplication::translate("ot::ui::Showcase", "Цвет"),
         [](QWidget* p) { return new ColorSwatch(QColor(), p); }},
    };
    const std::vector<std::pair<QString, Factory>> lists = {
        {QApplication::translate("ot::ui::Showcase", "Строка списка"),
         [](QWidget* p) {
             auto* r = new ListRow(QApplication::translate("ot::ui::Showcase", "Добавить комментарии"),
                                   QStringLiteral("message-square"), p);
             r->setPinnable(true);
             r->setFixedWidth(Theme::sizes().grid * 52);
             return r;
         }},
        {QApplication::translate("ot::ui::Showcase", "Выбранная строка"),
         [](QWidget* p) {
             auto* r = new ListRow(QApplication::translate("ot::ui::Showcase", "Закладки"),
                                   QStringLiteral("bookmark"), p);
             r->setDetail(QApplication::translate("ot::ui::Showcase", "12"));
             r->setSelected(true);
             r->setFixedWidth(Theme::sizes().grid * 52);
             return r;
         }},
        {QApplication::translate("ot::ui::Showcase", "Чип"),
         [](QWidget* p) {
             auto* c = new Chip(QApplication::translate("ot::ui::Showcase", "Подписано"), Tone::Ok, p);
             c->setIconName(QStringLiteral("circle-check"));
             return c;
         }},
        {QApplication::translate("ot::ui::Showcase", "Фильтр"),
         [](QWidget* p) {
             auto* c = new Chip(QApplication::translate("ot::ui::Showcase", "Мои"), Tone::Neutral, p);
             c->setCheckable(true);
             c->setClosable(true);
             return c;
         }},
        {QApplication::translate("ot::ui::Showcase", "Кнопка"),
         [](QWidget* p) {
             return new QPushButton(QApplication::translate("ot::ui::Showcase", "Отмена"), p);
         }},
        {QApplication::translate("ot::ui::Showcase", "Главная кнопка"),
         [](QWidget* p) {
             auto* b = new QPushButton(QApplication::translate("ot::ui::Showcase", "Сохранить"), p);
             b->setProperty("primary", true);
             return b;
         }},
    };
    // Одна таблица состояний слева, панель свойств с пустым состоянием справа — всё в 1440 × 900.
    std::vector<std::pair<QString, Factory>> rows = controls;
    rows.insert(rows.end(), lists.begin(), lists.end());
    auto* top = new QHBoxLayout();
    top->setSpacing(m.grid * 4);
    top->addWidget(stateTable(rows, page), 0, Qt::AlignTop);
    auto* panel = new QWidget(page);
    panel->setFixedWidth(m.rightPanel);
    auto* panelColumn = new QVBoxLayout(panel);
    panelColumn->setContentsMargins(0, 0, 0, 0);
    panelColumn->setSpacing(0);
    auto* header = new PanelHeader(QApplication::translate("ot::ui::Showcase", "Свойства"), panel);
    header->setBackText(QApplication::translate("ot::ui::Showcase", "Все инструменты"));
    header->addAction(QStringLiteral("ellipsis"), QApplication::translate("ot::ui::Showcase", "Ещё"));
    panelColumn->addWidget(header);
    auto* sheet = new PropertySheet(panel);
    sheet->addSection(QApplication::translate("ot::ui::Showcase", "Внешний вид"));
    sheet->addRow(QApplication::translate("ot::ui::Showcase", "&Цвет"), new ColorSwatch(QColor(), sheet));
    auto* width = new NumberField(sheet);
    width->setRange(0, 12);
    width->setUnit(QApplication::translate("ot::ui::Showcase", "пт"));
    width->setValue(1);
    sheet->addRow(QApplication::translate("ot::ui::Showcase", "&Толщина"), width);
    panelColumn->addWidget(sheet);
    auto* empty = new EmptyState(QStringLiteral("inbox"),
                                 QApplication::translate("ot::ui::Showcase", "Комментариев пока нет"),
                                 QApplication::translate("ot::ui::Showcase", "Добавить комментарий"), panel);
    panelColumn->addWidget(empty);
    panelColumn->addStretch(1);
    top->addWidget(panel, 0, Qt::AlignTop);
    top->addStretch(1);
    column->addLayout(top);

    // Карточки (обычная, наведение, выбранная), плавающая панель и тост.
    auto* blocks = new QHBoxLayout();
    blocks->setSpacing(m.grid * 4);
    const std::pair<ShowcaseState, bool> cardStates[] = {
        {ShowcaseState::Normal, false}, {ShowcaseState::Hover, false}, {ShowcaseState::Normal, true}};
    for (const auto& [state, selected] : cardStates) {
        auto* card = new Card(QApplication::translate("ot::ui::Showcase", "Договор поставки.pdf"),
                              QApplication::translate("ot::ui::Showcase", "Изменён сегодня, 12 страниц"),
                              QStringLiteral("file-text"), page);
        card->setClickable(true);
        card->setSelected(selected);
        card->setFixedWidth(m.leftPanel - m.grid * 8);
        forceShowcaseState(card, state);
        blocks->addWidget(card, 0, Qt::AlignTop);
    }

    auto* floatingHost = new QVBoxLayout();
    auto* bar = new FloatingBar(page);
    bar->addButton(QStringLiteral("select"), QStringLiteral("mouse-pointer-2"),
                   QApplication::translate("ot::ui::Showcase", "Выделение"));
    bar->addButton(QStringLiteral("hand"), QStringLiteral("hand"),
                   QApplication::translate("ot::ui::Showcase", "Рука"));
    bar->addSeparator();
    bar->addButton(QStringLiteral("note"), QStringLiteral("sticky-note"),
                   QApplication::translate("ot::ui::Showcase", "Записка"));
    bar->addButton(QStringLiteral("highlight"), QStringLiteral("highlighter"),
                   QApplication::translate("ot::ui::Showcase", "Выделить текст"));
    bar->addButton(QStringLiteral("draw"), QStringLiteral("pencil"),
                   QApplication::translate("ot::ui::Showcase", "Рисовать"));
    bar->addButton(QStringLiteral("text"), QStringLiteral("type"),
                   QApplication::translate("ot::ui::Showcase", "Добавить текст"));
    bar->addButton(QStringLiteral("sign"), QStringLiteral("signature"),
                   QApplication::translate("ot::ui::Showcase", "Подпись"));
    bar->addSeparator();
    bar->addButton(QStringLiteral("zoom"), QStringLiteral("zoom-in"),
                   QApplication::translate("ot::ui::Showcase", "Масштаб"));
    bar->setCurrent(QStringLiteral("select"));
    floatingHost->addWidget(bar, 0, Qt::AlignLeft);
    floatingHost->addSpacing(m.grid * 2);
    auto* toast =
        new Toast(QApplication::translate("ot::ui::Showcase", "Не удалось сохранить файл: диск заполнен."),
                  Tone::Danger, page);
    toast->setActionText(QApplication::translate("ot::ui::Showcase", "Подробнее"));
    toast->setFixedWidth(m.leftPanel + m.grid * 8);
    floatingHost->addWidget(toast, 0, Qt::AlignLeft);
    floatingHost->addStretch(1);
    blocks->addLayout(floatingHost, 1);
    column->addLayout(blocks);

    // Полосы сообщений всех тонов.
    auto* bars = new QGridLayout();
    bars->setHorizontalSpacing(m.grid * 4);
    bars->setVerticalSpacing(m.grid * 2);
    auto* info = new MessageBar(
        Tone::Info,
        QApplication::translate("ot::ui::Showcase",
                                "Этот файл заявляет соответствие PDF/A и открыт только для чтения."),
        page);
    info->addAction(QApplication::translate("ot::ui::Showcase", "Включить редактирование"),
                    QStringLiteral("edit"));
    auto* warn = new MessageBar(
        Tone::Warn,
        QApplication::translate("ot::ui::Showcase", "Защищённый просмотр: файл получен из интернета."), page);
    warn->addAction(QApplication::translate("ot::ui::Showcase", "Разрешить"), QStringLiteral("trust"), true);
    auto* form = new MessageBar(
        Tone::Form, QApplication::translate("ot::ui::Showcase", "Документ содержит поля формы."), page);
    form->addAction(QApplication::translate("ot::ui::Showcase", "Подсветить поля"),
                    QStringLiteral("highlight"));
    auto* danger =
        new MessageBar(Tone::Danger,
                       QApplication::translate("ot::ui::Showcase",
                                               "Подпись недействительна: документ изменён после подписания."),
                       page);
    danger->addAction(QApplication::translate("ot::ui::Showcase", "Панель подписей"),
                      QStringLiteral("signatures"));
    bars->addWidget(info, 0, 0);
    bars->addWidget(warn, 0, 1);
    bars->addWidget(form, 1, 0);
    bars->addWidget(danger, 1, 1);
    column->addLayout(bars);
    column->addStretch(1);
    // Фокус — у самой витрины: иначе первая кнопка «Обычное» получила бы настоящую рамку фокуса.
    page->setFocusPolicy(Qt::StrongFocus);
    page->setFocus();
    return page;
}

}  // namespace

void forceShowcaseState(QWidget* widget, ShowcaseState state) {
    QWidget* target = stateTarget(widget);
    switch (state) {
        case ShowcaseState::Hover:
            target->setAttribute(Qt::WA_UnderMouse, true);
            break;
        case ShowcaseState::Focus:
            widget->setProperty("otFocus", true);
            target->setProperty("otFocus", true);
            repolish(widget);
            break;
        case ShowcaseState::Disabled:
            widget->setEnabled(false);
            break;
        case ShowcaseState::Normal:
            break;
    }
}

QWidget* createShowcase(QWidget* parent) { return buildShowcase(parent); }

QWidget* createShowcaseWindow() {
    auto* window = new QWidget();
    window->setObjectName(QStringLiteral("showcaseWindow"));
    window->setWindowTitle(QApplication::translate("ot::ui::Showcase", "АкроБатя — витрина компонентов"));
    window->resize(1440, 900);
    auto* column = new QVBoxLayout(window);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    auto* controls = new QWidget(window);
    controls->setObjectName(QStringLiteral("showcaseControls"));
    auto* row = new QHBoxLayout(controls);
    auto* themes = new SegmentedBar(controls);
    themes->addSegment(QApplication::translate("ot::ui::Showcase", "Светлая"));
    themes->addSegment(QApplication::translate("ot::ui::Showcase", "Тёмная"));
    themes->addSegment(QApplication::translate("ot::ui::Showcase", "Высокая контрастность"));
    themes->addSegment(QApplication::translate("ot::ui::Showcase", "Системная"));
    const ThemeChoice choices[] = {ThemeChoice::Light, ThemeChoice::Dark, ThemeChoice::HighContrast,
                                   ThemeChoice::System};
    for (int i = 0; i < 4; ++i) {
        if (choices[i] == Theme::instance().choice()) {
            themes->setCurrentIndex(i);
        }
    }
    auto* large =
        new Chip(QApplication::translate("ot::ui::Showcase", "Крупные элементы"), Tone::Neutral, controls);
    large->setCheckable(true);
    large->setChecked(Theme::instance().scale() > 1.0);
    row->addWidget(themes);
    row->addWidget(large);
    row->addStretch(1);
    column->addWidget(controls);
    auto* scroll = new QScrollArea(window);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(createShowcase());
    column->addWidget(scroll, 1);
    // Новая тема или масштаб — витрина пересобирается (размеры у неё заданы при создании).
    auto rebuild = [scroll, themes, large, choices] {
        auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
        Theme::apply(*app, choices[std::max(0, themes->currentIndex())], large->isChecked() ? 1.25 : 1.0);
        if (QWidget* old = scroll->takeWidget()) {
            old->deleteLater();
        }
        scroll->setWidget(createShowcase());
    };
    QObject::connect(themes, &SegmentedBar::currentChanged, window, rebuild);
    QObject::connect(large, &Chip::toggled, window, rebuild);
    return window;
}

}  // namespace ot::ui
