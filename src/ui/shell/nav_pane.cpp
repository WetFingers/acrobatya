#include "ui/shell/nav_pane.h"

#include <QContextMenuEvent>
#include <QEvent>
#include <QMenu>
#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

#include "services/app_info.h"
#include "ui/panes/attachments_pane.h"
#include "ui/panes/bookmarks_pane.h"
#include "ui/panes/destinations_pane.h"
#include "ui/panes/layers_pane.h"
#include "ui/panes/nav_page.h"
#include "ui/panes/thumbnails_pane.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/widgets/empty_state.h"
#include "ui/widgets/panel_header.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

namespace {

// Страница будущего этапа: пустое состояние «появится в версии 0.N».
class StubNavPage : public NavPage {
public:
    StubNavPage(QString id, int stage, const QString& icon, QWidget* parent)
        : NavPage(parent), id_(std::move(id)), stage_(stage) {
        setObjectName(id_ + QStringLiteral("Pane"));
        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(0, 0, 0, 0);
        empty_ = new EmptyState(icon, QString(), QString(), this);
        column->addWidget(empty_);
        retranslate();
    }
    QString paneId() const override { return id_; }
    QString title() const override {
        if (id_ == QLatin1String("comments")) {
            return tr("Комментарии");
        }
        if (id_ == QLatin1String("signatures")) {
            return tr("Подписи");
        }
        return tr("Поля");
    }
    void setDocument(DocumentView*) override {}
    void retranslate() {
        empty_->setText(tr("Панель «%1» появится в версии 0.%2.").arg(title()).arg(stage_));
    }

protected:
    void changeEvent(QEvent* event) override {
        if (event->type() == QEvent::LanguageChange) {
            retranslate();
        }
        NavPage::changeEvent(event);
    }

private:
    QString id_;
    int stage_;
    EmptyState* empty_ = nullptr;
};

QSettings navSettings() { return QSettings(app::settingsPath(), QSettings::IniFormat); }

}  // namespace

// ---------------------------------------------------------------- NavPanel

NavPanel::NavPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("navPanel"));
    setAttribute(Qt::WA_StyledBackground);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(1, 0, 0, 0);  // левая рамка 1 px (QSS) не закрыта содержимым
    column->setSpacing(0);
    header_ = new PanelHeader(QString(), this);
    close_ = header_->addAction(QStringLiteral("x"), QString());
    close_->setObjectName(QStringLiteral("navPanelClose"));
    column->addWidget(header_);
    pages_ = new QStackedWidget(this);
    column->addWidget(pages_, 1);
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &NavPanel::applyMetrics);
}

void NavPanel::setPreferredWidth(int width) {
    preferred_ = width;
    updateGeometry();
}

QSize NavPanel::sizeHint() const {
    const QSize base = QWidget::sizeHint();
    return {preferred_ > 0 ? preferred_ : Theme::sizes().rightPanel, base.height()};
}

void NavPanel::applyMetrics() {
    const Metrics& m = Theme::sizes();
    setMinimumWidth(m.rightPanelMin);
    setMaximumWidth(m.rightPanelMax);
}

// ---------------------------------------------------------------- NavPane

QStringList NavPane::paneIds() {
    return {QStringLiteral("thumbnails"), QStringLiteral("bookmarks"),   QStringLiteral("comments"),
            QStringLiteral("layers"),     QStringLiteral("attachments"), QStringLiteral("signatures"),
            QStringLiteral("fields"),     QStringLiteral("destinations")};
}

NavPane::NavPane(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("navRail"));
    setAttribute(Qt::WA_StyledBackground);
    rail_ = new QVBoxLayout(this);
    rail_->setSpacing(0);
    panel_ = new NavPanel();
    connect(panel_->closeButton(), &QToolButton::clicked, this, &NavPane::closePanel);

    for (const QString& id : paneIds()) {
        auto* b = new ToolButton(paneIcon(id), QString(), this);
        b->setObjectName(QStringLiteral("nav.") + id);
        b->setCheckable(true);
        connect(b, &QToolButton::clicked, this, [this, id] {
            if (open_ && current_ == id) {
                closePanel();
            } else {
                showPane(id);
            }
        });
        rail_->addWidget(b, 0, Qt::AlignHCenter);
        buttons_.insert(id, b);
    }
    rail_->addStretch(1);

    addPage(new ThumbnailsPane(panel_->pages()));
    addPage(new BookmarksPane(panel_->pages()));
    addPage(new StubNavPage(QStringLiteral("comments"), 5, paneIcon(QStringLiteral("comments")),
                            panel_->pages()));
    addPage(new LayersPane(panel_->pages()));
    auto* attachments = new AttachmentsPane(panel_->pages());
    connect(attachments, &AttachmentsPane::openPdfRequested, this, &NavPane::openPdfRequested);
    addPage(attachments);
    addPage(new StubNavPage(QStringLiteral("signatures"), 4, paneIcon(QStringLiteral("signatures")),
                            panel_->pages()));
    addPage(
        new StubNavPage(QStringLiteral("fields"), 6, paneIcon(QStringLiteral("fields")), panel_->pages()));
    addPage(new DestinationsPane(panel_->pages()));

    QSettings s = navSettings();
    open_ = s.value(QLatin1String(kOpenKey), false).toBool();
    const QString saved = s.value(QLatin1String(kCurrentKey), current_).toString();
    if (pages_.contains(saved) && saved != QLatin1String("fields")) {
        current_ = saved;
    }
    buttons_.value(QStringLiteral("fields"))->setVisible(false);
    buttons_.value(QStringLiteral("destinations"))
        ->setVisible(s.value(QLatin1String(kDestinationsKey), false).toBool());

    retranslate();
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &NavPane::applyMetrics);
    applyState();
}

// Панель — не ребёнок полосы (её место — сплиттер окна): удаляется с полосой, если окно её не забрало.
NavPane::~NavPane() {
    if (panel_ != nullptr && panel_->parent() == nullptr) {
        delete panel_;
    }
}

void NavPane::addPage(NavPage* page) {
    pages_.insert(page->paneId(), page);
    panel_->pages()->addWidget(page);
}

void NavPane::replacePage(NavPage* page) {
    const QString id = page->paneId();
    if (NavPage* old = pages_.value(id)) {
        panel_->pages()->removeWidget(old);
        old->deleteLater();
    }
    page->setParent(panel_->pages());
    addPage(page);
    page->setDocument(view_);
    applyState();
}

QString NavPane::paneIcon(const QString& id) const {
    const char* name = toolIconName(id.toStdString());
    return name != nullptr ? QLatin1String(name) : QStringLiteral("file");
}

QString NavPane::paneTitle(const QString& id) const {
    if (id == QLatin1String("destinations")) {
        return tr("Назначения");
    }
    NavPage* p = pages_.value(id);
    return p != nullptr ? p->title() : id;
}

void NavPane::setDocument(DocumentView* view) {
    view_ = view;
    for (NavPage* p : std::as_const(pages_)) {
        p->setDocument(view);
    }
    if (open_ && shown_) {
        if (NavPage* p = pages_.value(current_)) {
            p->activated();
        }
    }
}

void NavPane::showPane(const QString& id) {
    if (!pages_.contains(id)) {
        return;
    }
    const bool changed = current_ != id;
    const bool opened = !open_;
    current_ = id;
    open_ = true;
    applyState();
    saveState();
    if (changed) {
        emit currentPaneChanged(id);
    }
    if (opened) {
        emit panelOpenChanged(true);
    }
}

void NavPane::closePanel() {
    if (!open_) {
        return;
    }
    open_ = false;
    applyState();
    saveState();
    emit panelOpenChanged(false);
}

void NavPane::togglePanel() {
    if (open_) {
        closePanel();
    } else {
        showPane(current_ == QLatin1String("fields") && !fields_ ? QStringLiteral("thumbnails") : current_);
    }
}

void NavPane::setShown(bool shown) {
    shown_ = shown;
    applyState();
}

void NavPane::setFieldsVisible(bool visible) {
    fields_ = visible;
    buttons_.value(QStringLiteral("fields"))->setVisible(visible);
    if (!visible && current_ == QLatin1String("fields") && open_) {
        showPane(QStringLiteral("thumbnails"));
    }
}

void NavPane::setDestinationsButton(bool on) {
    buttons_.value(QStringLiteral("destinations"))->setVisible(on);
    navSettings().setValue(QLatin1String(kDestinationsKey), on);
    if (!on && current_ == QLatin1String("destinations") && open_) {
        closePanel();
    }
}

bool NavPane::hasDestinationsButton() const {
    return !buttons_.value(QStringLiteral("destinations"))->isHidden();
}

int NavPane::panelWidth() const {
    const Metrics& m = Theme::sizes();
    const int w = navSettings().value(QLatin1String(kWidthKey), m.rightPanel).toInt();
    return std::clamp(w, m.rightPanelMin, m.rightPanelMax);
}

void NavPane::setPanelWidth(int width) {
    const Metrics& m = Theme::sizes();
    navSettings().setValue(QLatin1String(kWidthKey), std::clamp(width, m.rightPanelMin, m.rightPanelMax));
}

void NavPane::applyState() {
    panel_->setPreferredWidth(panelWidth());
    setVisible(shown_);
    panel_->setVisible(shown_ && open_);
    for (auto it = buttons_.constBegin(); it != buttons_.constEnd(); ++it) {
        it.value()->setChecked(open_ && it.key() == current_);
    }
    NavPage* p = pages_.value(current_);
    if (p != nullptr) {
        panel_->pages()->setCurrentWidget(p);
        panel_->header()->setTitle(paneTitle(current_));
        if (open_ && shown_) {
            p->activated();
        }
    }
}

void NavPane::saveState() const {
    QSettings s = navSettings();
    s.setValue(QLatin1String(kOpenKey), open_);
    s.setValue(QLatin1String(kCurrentKey), current_);
}

QMenu* NavPane::railMenu() {
    if (menu_ == nullptr) {
        menu_ = new QMenu(this);
        menu_->setObjectName(QStringLiteral("navRailMenu"));
    }
    menu_->clear();
    for (const QString& id : paneIds()) {
        if (id == QLatin1String("fields") && !fields_) {
            continue;
        }
        QAction* a = menu_->addAction(Icons::icon(paneIcon(id)), paneTitle(id));
        a->setObjectName(QStringLiteral("navMenu.") + id);
        a->setCheckable(true);
        a->setChecked(open_ && current_ == id);
        connect(a, &QAction::triggered, this, [this, id] {
            if (id == QLatin1String("destinations")) {
                setDestinationsButton(true);  // как «Добавить на правую панель» в Acrobat
            }
            showPane(id);
        });
    }
    if (hasDestinationsButton()) {
        menu_->addSeparator();
        QAction* hide = menu_->addAction(tr("Убрать «Назначения» с полосы"));
        hide->setObjectName(QStringLiteral("navMenu.hideDestinations"));
        connect(hide, &QAction::triggered, this, [this] { setDestinationsButton(false); });
    }
    return menu_;
}

void NavPane::contextMenuEvent(QContextMenuEvent* event) {
    railMenu()->popup(event->globalPos());
    event->accept();
}

void NavPane::retranslate() {
    for (auto it = buttons_.constBegin(); it != buttons_.constEnd(); ++it) {
        it.value()->setLabel(paneTitle(it.key()));
    }
    panel_->closeButton()->setLabel(tr("Закрыть панель (F4)"));
    panel_->header()->setTitle(paneTitle(current_));
    setAccessibleName(tr("Панель навигации"));
}

void NavPane::applyMetrics() {
    const Metrics& m = Theme::sizes();
    setFixedWidth(m.navButton);
    rail_->setContentsMargins(1, m.grid, 0, m.grid);  // левая рамка 1 px
    for (ToolButton* b : std::as_const(buttons_)) {
        b->setFixedSize(m.navButton - m.grid * 2, m.navButton - m.grid * 2);
    }
}

void NavPane::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

}  // namespace ot::ui
