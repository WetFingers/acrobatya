#include "ui/panes/layers_pane.h"

#include <QEvent>
#include <QFuture>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>

#include "services/nav_ops.h"
#include "ui/shell/document_view.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/widgets/empty_state.h"

namespace ot::ui {

namespace {

constexpr int kLayerIndexRole = Qt::UserRole + 1;

// Иконка строки — готовый растр тона: делегат дерева рисует раскрытые строки состоянием On, а QIcon темы
// красит On в accent.
QIcon layerIcon(const QString& name, IconTone tone) {
    return QIcon(Icons::get(name, tone, Theme::sizes().iconRow));
}

}  // namespace

LayersPane::LayersPane(QWidget* parent) : NavPage(parent) {
    setObjectName(QStringLiteral("layersPane"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    stack_ = new QStackedWidget(this);
    tree_ = new QTreeWidget(stack_);
    tree_->setObjectName(QStringLiteral("layerTree"));
    tree_->setFrameShape(QFrame::NoFrame);
    tree_->setHeaderHidden(true);
    tree_->setColumnCount(1);
    tree_->setRootIsDecorated(true);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    empty_ = new EmptyState(QStringLiteral("layers"), QString(), QString(), stack_);
    stack_->addWidget(tree_);
    stack_->addWidget(empty_);
    column->addWidget(stack_, 1);
    // Внизу — «Сбросить к исходному» (как «Параметры → Восстановить исходную видимость» Acrobat).
    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("paneButtons"));
    auto* row = new QHBoxLayout(bar);
    const Metrics& m = Theme::sizes();
    row->setContentsMargins(m.grid * 2, m.grid * 2, m.grid * 2, m.grid * 2);
    reset_ = new QPushButton(bar);
    reset_->setObjectName(QStringLiteral("layersReset"));
    reset_->setIcon(Icons::icon(QStringLiteral("rotate-ccw")));
    connect(reset_, &QPushButton::clicked, this, &LayersPane::resetToDefault);
    row->addWidget(reset_);
    row->addStretch(1);
    column->addWidget(bar);
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        if (item != nullptr) {
            toggle(item->data(0, kLayerIndexRole).toInt());
        }
    });
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        if (item != nullptr) {
            toggle(item->data(0, kLayerIndexRole).toInt());
        }
    });
    connect(&Theme::instance(), &Theme::themeChanged, this,
            [this] { fill(std::vector<layers::LayerUi>(rows_)); });
    retranslate();
}

QString LayersPane::title() const { return tr("Слои"); }

bool LayersPane::isEmptyShown() const { return stack_->currentWidget() == empty_; }

void LayersPane::setDocument(DocumentView* view) {
    if (view == view_ && !dirty_) {
        return;
    }
    if (view_ != nullptr) {
        disconnect(&view_->session(), nullptr, this, nullptr);
    }
    view_ = view;
    dirty_ = true;
    ++generation_;
    if (view_ != nullptr) {
        // Слои сменил кто-то ещё (сброс после записи) — перечитать.
        connect(&view_->session(), &DocumentSession::layersChanged, this, [this] {
            if (isVisible()) {
                reload();
            } else {
                dirty_ = true;
            }
        });
    }
    if (isVisible()) {
        reload();
    } else {
        fill({});
    }
}

void LayersPane::activated() {
    if (dirty_) {
        reload();
    }
}

void LayersPane::reload() {
    dirty_ = false;
    const quint64 generation = ++generation_;
    if (view_ == nullptr) {
        fill({});
        return;
    }
    nav::layers(view_->session())
        .then(this,
              [this, generation](const std::vector<layers::LayerUi>& rows) {
                  if (generation == generation_) {
                      fill(rows);
                  }
              })
        .onFailed(this, [this, generation] {
            if (generation == generation_) {
                fill({});
            }
        });
}

void LayersPane::fill(const std::vector<layers::LayerUi>& rows) {
    rows_ = rows;
    tree_->clear();
    std::vector<QTreeWidgetItem*> parents;  // по глубине
    for (const layers::LayerUi& row : rows) {
        const int depth = std::max(0, row.depth);
        parents.resize(static_cast<std::size_t>(depth));
        QTreeWidgetItem* item = parents.empty() || parents.back() == nullptr
                                    ? new QTreeWidgetItem(tree_)
                                    : new QTreeWidgetItem(parents.back());
        const QString text = QString::fromStdString(row.text);
        item->setText(0, text);
        item->setData(0, kLayerIndexRole, row.index);
        if (row.kind == layers::UiKind::Label) {
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
            item->setToolTip(0, text);
        } else if (row.locked) {
            item->setIcon(0, layerIcon(QStringLiteral("lock"), IconTone::Muted));
            item->setToolTip(0, tr("«%1» — слой заблокирован, его видимость не меняется").arg(text));
        } else {
            item->setIcon(0, layerIcon(row.on ? QStringLiteral("eye") : QStringLiteral("eye-off"),
                                       row.on ? IconTone::Text : IconTone::Muted));
            item->setToolTip(0, row.on ? tr("«%1» — показан; щелчок скрывает").arg(text)
                                       : tr("«%1» — скрыт; щелчок показывает").arg(text));
        }
        item->setData(0, Qt::AccessibleDescriptionRole,
                      row.kind == layers::UiKind::Label ? QString()
                      : row.locked                      ? tr("заблокирован")
                      : row.on                          ? tr("показан")
                                                        : tr("скрыт"));
        parents.push_back(item);
    }
    tree_->expandAll();
    stack_->setCurrentWidget(rows.empty() ? static_cast<QWidget*>(empty_) : tree_);
    reset_->setEnabled(!rows.empty());
    emit rowsChanged();
}

QTreeWidgetItem* LayersPane::item(int index) const {
    for (QTreeWidgetItemIterator it(tree_); *it != nullptr; ++it) {
        if ((*it)->data(0, kLayerIndexRole).toInt() == index) {
            return *it;
        }
    }
    return nullptr;
}

void LayersPane::toggle(int index) {
    if (view_ == nullptr || index < 0 || index >= static_cast<int>(rows_.size())) {
        return;
    }
    const layers::LayerUi& row = rows_[static_cast<std::size_t>(index)];
    if (row.locked || row.kind == layers::UiKind::Label) {
        return;
    }
    const quint64 generation = ++generation_;
    nav::toggleLayer(view_->session(), index)
        .then(this, [this, generation](const std::vector<layers::LayerUi>& r) {
            if (generation == generation_) {
                fill(r);
            }
        });
}

void LayersPane::resetToDefault() {
    if (view_ == nullptr) {
        return;
    }
    const quint64 generation = ++generation_;
    nav::resetLayers(view_->session()).then(this, [this, generation](const std::vector<layers::LayerUi>& r) {
        if (generation == generation_) {
            fill(r);
        }
    });
}

void LayersPane::retranslate() {
    reset_->setText(tr("Сбросить к исходному"));
    empty_->setText(tr("В документе нет слоёв."));
    tree_->setAccessibleName(tr("Слои"));
}

void LayersPane::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    NavPage::changeEvent(event);
}

}  // namespace ot::ui
