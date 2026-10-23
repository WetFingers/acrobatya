#include "ui/shell/global_bar.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>

#include "ui/shell/document_tabs.h"
#include "ui/theme/theme.h"
#include "ui/widgets/search_field.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

GlobalBar::GlobalBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("globalBar"));
    setAttribute(Qt::WA_StyledBackground);
    auto* row = new QHBoxLayout(this);

    menu_ = new ToolButton(QStringLiteral("menu"), {}, this);
    menu_->setObjectName(QStringLiteral("globalMenu"));
    menu_->setPopupMode(QToolButton::InstantPopup);
    menu_->setLabelVisible(true);
    home_ = new ToolButton(QStringLiteral("home"), {}, this);
    home_->setObjectName(QStringLiteral("globalHome"));
    home_->setCheckable(true);
    home_->setLabelVisible(true);
    connect(home_, &QToolButton::clicked, this, &GlobalBar::homeClicked);
    separator_ = new QFrame(this);
    separator_->setObjectName(QStringLiteral("globalSeparator"));
    tabs_ = new DocumentTabs(this);

    search_ = new SearchField(this);
    search_->setObjectName(QStringLiteral("globalSearch"));
    connect(search_, &SearchField::searchRequested, this, &GlobalBar::searchRequested);
    allTools_ = new ToolButton(QStringLiteral("grid-2x2"), {}, this);
    allTools_->setObjectName(QStringLiteral("globalAllTools"));
    allTools_->setLabelVisible(true);
    connect(allTools_, &QToolButton::clicked, this, &GlobalBar::allToolsClicked);
    share_ = new ToolButton(QStringLiteral("share-2"), {}, this);
    share_->setObjectName(QStringLiteral("globalShare"));
    share_->setPopupMode(QToolButton::InstantPopup);
    share_->setLabelVisible(true);
    settings_ = new ToolButton(QStringLiteral("settings"), {}, this);
    settings_->setObjectName(QStringLiteral("globalSettings"));
    connect(settings_, &QToolButton::clicked, this, &GlobalBar::settingsClicked);

    row->addWidget(menu_);
    row->addWidget(home_);
    row->addWidget(separator_);
    row->addWidget(tabs_, 1, Qt::AlignVCenter);
    row->addWidget(search_);
    row->addWidget(allTools_);
    row->addWidget(share_);
    row->addWidget(settings_);
    retranslate();
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &GlobalBar::applyMetrics);
}

void GlobalBar::setMainMenu(QMenu* menu) { menu_->setMenu(menu); }

void GlobalBar::setShareMenu(QMenu* menu) { share_->setMenu(menu); }

void GlobalBar::setHomeActive(bool active) { home_->setChecked(active); }

void GlobalBar::retranslate() {
    menu_->setLabel(tr("Меню"));
    menu_->setToolTip(tr("Меню (F9 — строка меню)"));
    home_->setLabel(tr("Главная"));
    allTools_->setLabel(tr("Все инструменты"));
    share_->setLabel(tr("Поделиться"));
    settings_->setLabel(tr("Установки (Ctrl+K)"));
    search_->setPlaceholderText(tr("Поиск"));
    setAccessibleName(tr("Глобальная панель"));
}

void GlobalBar::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

void GlobalBar::applyMetrics() {
    const Metrics& m = Theme::sizes();
    setFixedHeight(m.globalBar);
    auto* row = static_cast<QHBoxLayout*>(layout());
    row->setContentsMargins(m.grid * 2, 0, m.grid * 2, 0);
    row->setSpacing(m.grid);
    separator_->setFixedSize(1, m.iconPanel + m.grid * 2);
    search_->setFixedWidth(m.searchWidth);
}

}  // namespace ot::ui
