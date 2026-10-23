#include "ui/shell/tool_panel.h"

#include <QEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/tool_registry.h"
#include "ui/widgets/list_row.h"
#include "ui/widgets/panel_header.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

ToolPanel::ToolPanel(ToolRegistry& registry, QWidget* parent) : QWidget(parent), registry_(registry) {
    setObjectName(QStringLiteral("toolPanel"));
    setAttribute(Qt::WA_StyledBackground);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 1, 0);  // правая рамка 1 px (QSS) не закрыта содержимым
    column->setSpacing(0);
    pages_ = new QStackedWidget(this);
    column->addWidget(pages_);

    // «Все инструменты»
    listPage_ = new QWidget(pages_);
    listPage_->setObjectName(QStringLiteral("allTools"));
    auto* list = new QVBoxLayout(listPage_);
    list->setContentsMargins(0, 0, 0, 0);
    list->setSpacing(0);
    listHeader_ = new PanelHeader(QString(), listPage_);
    collapseList_ = listHeader_->addAction(QStringLiteral("panel-left-close"), QString());
    connect(collapseList_, &QToolButton::clicked, this, [this] { setCollapsed(true); });
    list->addWidget(listHeader_);
    scroll_ = new QScrollArea(listPage_);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rowsHost_ = new QWidget(scroll_);
    rowsHost_->setObjectName(QStringLiteral("allToolsRows"));
    rowsLayout_ = new QVBoxLayout(rowsHost_);
    scroll_->setWidget(rowsHost_);
    list->addWidget(scroll_, 1);
    pages_->addWidget(listPage_);

    // Панель активного инструмента
    toolPage_ = new QWidget(pages_);
    toolPage_->setObjectName(QStringLiteral("toolPage"));
    auto* tool = new QVBoxLayout(toolPage_);
    tool->setContentsMargins(0, 0, 0, 0);
    tool->setSpacing(0);
    toolHeader_ = new PanelHeader(QString(), toolPage_);
    collapseTool_ = toolHeader_->addAction(QStringLiteral("panel-left-close"), QString());
    connect(collapseTool_, &QToolButton::clicked, this, [this] { setCollapsed(true); });
    connect(toolHeader_, &PanelHeader::backClicked, this, &ToolPanel::showList);
    tool->addWidget(toolHeader_);
    toolLayout_ = new QVBoxLayout();
    toolLayout_->setContentsMargins(0, 0, 0, 0);
    tool->addLayout(toolLayout_, 1);
    pages_->addWidget(toolPage_);

    // Свёрнутая полоса 48
    rail_ = new QWidget(pages_);
    rail_->setObjectName(QStringLiteral("toolRail"));
    railLayout_ = new QVBoxLayout(rail_);
    pages_->addWidget(rail_);

    connect(&registry_, &ToolRegistry::toolsChanged, this, &ToolPanel::rebuild);
    connect(&registry_, &ToolRegistry::pinnedChanged, this, &ToolPanel::rebuild);
    connect(&registry_, &ToolRegistry::activated, this, &ToolPanel::onActivated);
    connect(&registry_, &ToolRegistry::deactivated, this, [this] {
        unhost();
        if (!collapsed_) {
            pages_->setCurrentWidget(listPage_);
        }
    });
    connect(&Theme::instance(), &Theme::themeChanged, this, &ToolPanel::applyMetrics);
    retranslate();
    rebuild();
    applyMetrics();
    if (registry_.active() != nullptr) {
        onActivated(registry_.active());
    }
}

// Панель инструмента — его виджет: отдать назад до того, как Qt удалит детей.
ToolPanel::~ToolPanel() { unhost(); }

void ToolPanel::setCollapsed(bool collapsed) {
    if (collapsed_ == collapsed) {
        return;
    }
    collapsed_ = collapsed;
    if (collapsed) {
        pages_->setCurrentWidget(rail_);
    } else {
        pages_->setCurrentWidget(registry_.active() != nullptr ? toolPage_ : listPage_);
    }
    applyMetrics();
    emit collapsedChanged(collapsed);
}

void ToolPanel::showList() {
    registry_.deactivate();
    unhost();
    if (!collapsed_) {
        pages_->setCurrentWidget(listPage_);
    }
}

bool ToolPanel::showsList() const { return pages_->currentWidget() == listPage_; }

QList<ListRow*> ToolPanel::rows() const {
    return rowsHost_->findChildren<ListRow*>(Qt::FindDirectChildrenOnly);
}

ListRow* ToolPanel::row(const QString& id) const {
    for (ListRow* r : rows()) {
        if (r->property("toolId").toString() == id) {
            return r;
        }
    }
    return nullptr;
}

QList<ToolButton*> ToolPanel::railButtons() const {
    QList<ToolButton*> out;
    for (ToolButton* b : rail_->findChildren<ToolButton*>(Qt::FindDirectChildrenOnly)) {
        if (b != expand_) {
            out << b;
        }
    }
    return out;
}

QString ToolPanel::hint(const Tool& tool) const {
    return tool.isStub() ? tr("«%1» появится в версии 0.%2").arg(tool.title()).arg(tool.stage())
                         : tool.title();
}

// Строки и столбец иконок — заново по реестру (порядок 6.2, закреплённые — первыми).
void ToolPanel::rebuild() {
    for (ListRow* r : rows()) {
        r->deleteLater();
        r->setParent(nullptr);
    }
    while (QLayoutItem* item = rowsLayout_->takeAt(0)) {
        delete item;
    }
    for (ToolButton* b : railButtons()) {
        b->setParent(nullptr);
        b->deleteLater();
    }
    while (QLayoutItem* item = railLayout_->takeAt(0)) {
        delete item;
    }
    if (expand_ == nullptr) {
        expand_ = new ToolButton(QStringLiteral("panel-left-open"), QString(), rail_);
        expand_->setObjectName(QStringLiteral("toolRailExpand"));
        connect(expand_, &QToolButton::clicked, this, [this] { setCollapsed(false); });
    }
    railLayout_->addWidget(expand_, 0, Qt::AlignHCenter);

    for (Tool* tool : registry_.listed()) {
        const QString id = tool->id();
        auto* r = new ListRow(tool->title(), tool->icon(), rowsHost_);
        r->setProperty("toolId", id);
        r->setAccessibleName(tool->title());
        r->setPinnable(true);
        r->setPinned(registry_.isPinned(id));
        r->setEnabled(!tool->isStub());
        r->setToolTip(hint(*tool));
        connect(r, &ListRow::clicked, this, [this, id] { emit toolRequested(id); });
        connect(r, &ListRow::pinToggled, this, [this, id](bool on) {
            // Список перестраивается позже: строка, которая шлёт сигнал, ещё на стеке.
            QMetaObject::invokeMethod(
                this, [this, id, on] { registry_.setPinned(id, on); }, Qt::QueuedConnection);
        });
        rowsLayout_->addWidget(r);

        auto* b = new ToolButton(QString(), tool->title(), rail_);
        b->setIconName(tool->icon(),
                       tool->isStub() ? IconTone::Disabled : IconTone::Text);  // как серая строка
        b->setProperty("toolId", id);
        b->setEnabled(!tool->isStub());
        b->setToolTip(hint(*tool));
        connect(b, &QToolButton::clicked, this, [this, id] {
            setCollapsed(false);
            emit toolRequested(id);
        });
        railLayout_->addWidget(b, 0, Qt::AlignHCenter);
    }
    rowsLayout_->addStretch(1);
    railLayout_->addStretch(1);
    applyMetrics();
}

void ToolPanel::onActivated(Tool* tool) {
    unhost();
    toolHeader_->setTitle(tool->title());
    if (QWidget* panel = tool->panel()) {
        hosted_ = panel;
        toolLayout_->addWidget(panel);
        panel->show();
    }
    if (!collapsed_) {
        pages_->setCurrentWidget(toolPage_);
    }
}

void ToolPanel::unhost() {
    if (hosted_ == nullptr) {
        return;
    }
    toolLayout_->removeWidget(hosted_);
    hosted_->hide();
    hosted_->setParent(nullptr);
    hosted_ = nullptr;
}

void ToolPanel::retranslate() {
    listHeader_->setTitle(tr("Все инструменты"));
    toolHeader_->setBackText(tr("Все инструменты"));
    collapseList_->setLabel(tr("Свернуть панель (Shift+F4)"));
    collapseTool_->setLabel(tr("Свернуть панель (Shift+F4)"));
    if (expand_ != nullptr) {
        expand_->setLabel(tr("Развернуть панель (Shift+F4)"));
    }
    setAccessibleName(tr("Панель инструментов"));
}

void ToolPanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
        rebuild();
    }
    QWidget::changeEvent(event);
}

void ToolPanel::applyMetrics() {
    const Metrics& m = Theme::sizes();
    setFixedWidth(collapsed_ ? m.leftPanelCollapsed : m.leftPanel);
    rowsLayout_->setContentsMargins(m.grid * 2, m.grid, m.grid * 2, m.grid * 2);
    rowsLayout_->setSpacing(0);
    railLayout_->setContentsMargins(0, m.grid * 2, 0, m.grid * 2);
    railLayout_->setSpacing(m.grid);
    if (expand_ != nullptr) {
        expand_->setLabel(tr("Развернуть панель (Shift+F4)"));
    }
}

}  // namespace ot::ui
