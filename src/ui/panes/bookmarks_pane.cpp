#include "ui/panes/bookmarks_pane.h"

#include <QEvent>
#include <QFuture>
#include <QHeaderView>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

#include "services/nav_ops.h"
#include "ui/shell/document_view.h"
#include "ui/view/page_view.h"
#include "ui/widgets/empty_state.h"

namespace ot::ui {

namespace {

QVariantList bookmarkPath(const std::vector<int>& path) {
    QVariantList out;
    for (const int i : path) {
        out << i;
    }
    return out;
}

std::vector<int> bookmarkPathFrom(const QVariantList& path) {
    std::vector<int> out;
    for (const QVariant& v : path) {
        out.push_back(v.toInt());
    }
    return out;
}

}  // namespace

BookmarksPane::BookmarksPane(QWidget* parent) : NavPage(parent) {
    setObjectName(QStringLiteral("bookmarksPane"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    stack_ = new QStackedWidget(this);
    tree_ = new QTreeView(stack_);
    tree_->setObjectName(QStringLiteral("bookmarkTree"));
    tree_->setFrameShape(QFrame::NoFrame);
    tree_->setHeaderHidden(true);
    tree_->setUniformRowHeights(false);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setWordWrap(true);
    tree_->setTextElideMode(Qt::ElideNone);
    model_ = new QStandardItemModel(this);
    tree_->setModel(model_);
    tree_->header()->setStretchLastSection(true);
    empty_ = new EmptyState(QStringLiteral("bookmark"), QString(), QString(), stack_);
    stack_->addWidget(tree_);
    stack_->addWidget(empty_);
    column->addWidget(stack_);

    connect(tree_, &QTreeView::expanded, this, [this](const QModelIndex& index) {
        QStandardItem* item = model_->itemFromIndex(index);
        if (item != nullptr && item->rowCount() == 1 && item->child(0)->data(PlaceholderRole).toBool()) {
            load(bookmarkPathFrom(item->data(PathRole).toList()));
        }
    });
    connect(tree_, &QTreeView::clicked, this, &BookmarksPane::activate);
    connect(tree_, &QTreeView::activated, this, &BookmarksPane::activate);
    retranslate();
}

QString BookmarksPane::title() const { return tr("Закладки"); }

bool BookmarksPane::isEmptyShown() const { return stack_->currentWidget() == empty_; }

void BookmarksPane::setDocument(DocumentView* view) {
    if (view == view_ && !dirty_) {
        return;
    }
    view_ = view;
    dirty_ = true;
    ++generation_;
    model_->clear();
    actions_.clear();
    if (isVisible()) {
        reload();
    }
}

void BookmarksPane::activated() {
    if (dirty_) {
        reload();
    }
}

void BookmarksPane::reload() {
    dirty_ = false;
    ++generation_;
    model_->clear();
    actions_.clear();
    if (view_ == nullptr) {
        stack_->setCurrentWidget(empty_);
        return;
    }
    stack_->setCurrentWidget(tree_);
    load({});
}

void BookmarksPane::load(const std::vector<int>& path) {
    if (view_ == nullptr) {
        return;
    }
    const quint64 generation = generation_;
    QElapsedTimer clock;
    clock.start();
    const QVariantList key = bookmarkPath(path);
    nav::outline(view_->session(), path)
        .then(this,
              [this, generation, clock, key](const std::vector<outline::Item>& items) {
                  if (generation != generation_) {
                      return;  // документ сменился
                  }
                  // Узел ищется заново: модель могла перестроиться, указатель — нет.
                  QStandardItem* root = model_->invisibleRootItem();
                  QStandardItem* node = root;
                  for (const QVariant& v : key) {
                      node = node != nullptr ? node->child(v.toInt()) : nullptr;
                  }
                  if (node == nullptr) {
                      return;
                  }
                  if (node != root) {
                      node->removeRows(0, node->rowCount());  // заглушка «…»
                  }
                  QList<QModelIndex> toExpand;
                  for (const outline::Item& b : items) {
                      auto* row = new QStandardItem(QString::fromStdString(b.title));
                      row->setEditable(false);
                      row->setToolTip(row->text());
                      QFont font = row->font();
                      font.setBold(b.bold);
                      font.setItalic(b.italic);
                      row->setFont(font);
                      if (b.color) {
                          row->setForeground(QColor((*b.color)[0], (*b.color)[1], (*b.color)[2]));
                      }
                      row->setData(bookmarkPath(b.path), PathRole);
                      row->setData(static_cast<int>(actions_.size()), ActionRole);
                      actions_.push_back(b.action);
                      if (b.hasChildren) {
                          auto* placeholder = new QStandardItem(QStringLiteral("…"));
                          placeholder->setData(true, PlaceholderRole);
                          placeholder->setEditable(false);
                          placeholder->setEnabled(false);
                          row->appendRow(placeholder);
                      }
                      node->appendRow(row);
                      if (b.hasChildren && b.open) {
                          toExpand << row->index();
                      }
                  }
                  if (node == root) {
                      stack_->setCurrentWidget(items.empty() ? static_cast<QWidget*>(empty_) : tree_);
                  }
                  for (const QModelIndex& index : toExpand) {
                      tree_->expand(index);  // /Count > 0 — раскрыта в файле: дети грузятся сразу
                  }
                  emit levelLoaded(key, static_cast<int>(items.size()), clock.elapsed());
              })
        .onFailed(this, [this, generation, key] {
            if (generation == generation_ && key.isEmpty()) {
                stack_->setCurrentWidget(empty_);
            }
        });
}

void BookmarksPane::activate(const QModelIndex& index) {
    if (view_ == nullptr || !index.isValid() || index.data(PlaceholderRole).toBool()) {
        return;
    }
    const int n = index.data(ActionRole).toInt();
    if (n < 0 || n >= static_cast<int>(actions_.size())) {
        return;
    }
    links::LinkInfo link;
    link.page = view_->view().currentPage();
    link.action = actions_[static_cast<std::size_t>(n)];
    view_->view().activateLink(link);
}

void BookmarksPane::retranslate() {
    empty_->setText(tr("В документе нет закладок."));
    tree_->setAccessibleName(tr("Закладки"));
}

void BookmarksPane::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    NavPage::changeEvent(event);
}

}  // namespace ot::ui
