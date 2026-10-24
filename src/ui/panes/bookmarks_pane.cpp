#include "ui/panes/bookmarks_pane.h"

#include <QEvent>
#include <QFuture>
#include <QHeaderView>
#include <QMenu>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

#include "services/document_session.h"
#include "services/nav_ops.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/theme/icons.h"
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

    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex index = tree_->indexAt(pos);
        if (view_ == nullptr || !index.isValid() || index.data(PlaceholderRole).toBool()) {
            return;
        }
        QMenu menu(this);
        QAction* go = menu.addAction(tr("П&ерейти к закладке"));
        connect(go, &QAction::triggered, this, [this, index] { activate(index); });
        QAction* print = menu.addAction(Icons::icon(QStringLiteral("printer")), tr("&Печать страниц…"));
        print->setEnabled(view_->permissions().print && !bookmarkPages(index).isEmpty());
        connect(print, &QAction::triggered, this, [this, index] { printBookmark(index); });
        menu.exec(tree_->viewport()->mapToGlobal(pos));
    });
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

int BookmarksPane::pageOf(const QModelIndex& index) const {
    if (!index.isValid() || index.data(PlaceholderRole).toBool()) {
        return -1;
    }
    const int n = index.data(ActionRole).toInt();
    if (n < 0 || n >= static_cast<int>(actions_.size())) {
        return -1;
    }
    const links::LinkAction& a = actions_[static_cast<std::size_t>(n)];
    return a.kind == links::LinkKind::GoTo && a.page >= 0 ? a.page : -1;
}

QList<int> BookmarksPane::bookmarkPages(const QModelIndex& index) const {
    const int start = pageOf(index);
    if (view_ == nullptr || start < 0) {
        return {};
    }
    int end = view_->session().pageCount() - 1;
    // Следующая закладка того же или более высокого уровня со страницей дальше этой.
    bool found = false;
    for (QModelIndex level = index; level.isValid() && !found; level = level.parent()) {
        for (QModelIndex next = level.sibling(level.row() + 1, 0); next.isValid() && !found;
             next = next.sibling(next.row() + 1, 0)) {
            const int page = pageOf(next);
            if (page > start) {
                end = page - 1;
                found = true;
            }
        }
    }
    QList<int> pages;
    for (int p = start; p <= end; ++p) {
        pages << p;
    }
    return pages;
}

void BookmarksPane::printBookmark(const QModelIndex& index) {
    const QList<int> pages = bookmarkPages(index);
    auto* window = view_ != nullptr ? qobject_cast<MainWindow*>(view_->window()) : nullptr;
    if (window != nullptr && !pages.isEmpty()) {
        window->showPrintDialog(pages);
    }
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
