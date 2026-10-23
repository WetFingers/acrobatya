#include "ui/panes/destinations_pane.h"

#include <QEvent>
#include <QFuture>
#include <QHeaderView>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "services/nav_ops.h"
#include "ui/shell/document_view.h"
#include "ui/view/page_view.h"
#include "ui/widgets/empty_state.h"

namespace ot::ui {

namespace {

constexpr int kDestRole = Qt::UserRole + 1;

// Страница — числом (сортировка по номеру, а не по строке).
class DestItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem& other) const override {
        const int column = treeWidget() != nullptr ? treeWidget()->sortColumn() : 0;
        if (column == 1) {
            return data(1, Qt::UserRole).toInt() < other.data(1, Qt::UserRole).toInt();
        }
        return text(0).compare(other.text(0), Qt::CaseInsensitive) < 0;
    }
};

}  // namespace

DestinationsPane::DestinationsPane(QWidget* parent) : NavPage(parent) {
    setObjectName(QStringLiteral("destinationsPane"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    stack_ = new QStackedWidget(this);
    tree_ = new QTreeWidget(stack_);
    tree_->setObjectName(QStringLiteral("destinationList"));
    tree_->setFrameShape(QFrame::NoFrame);
    tree_->setColumnCount(2);
    tree_->setRootIsDecorated(false);
    tree_->setUniformRowHeights(true);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setSortingEnabled(true);
    tree_->sortByColumn(0, Qt::AscendingOrder);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    empty_ = new EmptyState(QStringLiteral("locate"), QString(), QString(), stack_);
    stack_->addWidget(tree_);
    stack_->addWidget(empty_);
    column->addWidget(stack_);
    connect(tree_, &QTreeWidget::itemClicked, this, &DestinationsPane::activate);
    connect(tree_, &QTreeWidget::itemActivated, this, &DestinationsPane::activate);
    retranslate();
}

QString DestinationsPane::title() const { return tr("Назначения"); }

bool DestinationsPane::isEmptyShown() const { return stack_->currentWidget() == empty_; }

void DestinationsPane::setDocument(DocumentView* view) {
    if (view == view_ && !dirty_) {
        return;
    }
    view_ = view;
    dirty_ = true;
    ++generation_;
    if (isVisible()) {
        reload();
    } else {
        fill({});
    }
}

void DestinationsPane::activated() {
    if (dirty_) {
        reload();
    }
}

void DestinationsPane::reload() {
    dirty_ = false;
    const quint64 generation = ++generation_;
    if (view_ == nullptr) {
        fill({});
        return;
    }
    nav::destinations(view_->session())
        .then(this,
              [this, generation](const std::vector<dests::Destination>& list) {
                  if (generation == generation_) {
                      fill(list);
                  }
              })
        .onFailed(this, [this, generation] {
            if (generation == generation_) {
                fill({});
            }
        });
}

void DestinationsPane::fill(const std::vector<dests::Destination>& list) {
    dests_ = list;
    tree_->setSortingEnabled(false);
    tree_->clear();
    for (std::size_t i = 0; i < list.size(); ++i) {
        const dests::Destination& d = list[i];
        auto* item = new DestItem(tree_);
        item->setText(0, QString::fromStdString(d.name));
        const int page = d.action.page;
        item->setText(1, page >= 0 ? QString::number(page + 1) : QStringLiteral("—"));
        item->setData(1, Qt::UserRole, page);
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(0, kDestRole, static_cast<int>(i));
        item->setToolTip(0, page >= 0 ? tr("«%1» — страница %2").arg(item->text(0)).arg(page + 1)
                                      : tr("«%1» — назначение не найдено в документе").arg(item->text(0)));
    }
    tree_->setSortingEnabled(true);
    stack_->setCurrentWidget(list.empty() ? static_cast<QWidget*>(empty_) : tree_);
    emit listChanged();
}

void DestinationsPane::activate(QTreeWidgetItem* item) {
    if (view_ == nullptr || item == nullptr) {
        return;
    }
    const int n = item->data(0, kDestRole).toInt();
    if (n < 0 || n >= static_cast<int>(dests_.size()) ||
        dests_[static_cast<std::size_t>(n)].action.page < 0) {
        return;
    }
    links::LinkInfo link;
    link.page = view_->view().currentPage();
    link.action = dests_[static_cast<std::size_t>(n)].action;
    view_->view().activateLink(link);
}

void DestinationsPane::retranslate() {
    tree_->setHeaderLabels({tr("Имя"), tr("Страница")});
    tree_->setAccessibleName(tr("Назначения"));
    empty_->setText(tr("В документе нет именованных назначений."));
}

void DestinationsPane::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    NavPage::changeEvent(event);
}

}  // namespace ot::ui
