#include "ui/panes/thumbnails_pane.h"

#include <QAbstractListModel>
#include <QEvent>
#include <QFuture>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListView>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "core/render/display_cache.h"
#include "core/render/tiles.h"
#include "services/document_session.h"
#include "services/render_pool.h"
#include "ui/shell/document_view.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"

namespace ot::ui {

namespace {

// Ниже тайлов и превью вида (tile_painter.cpp: запас — от 100 000): миниатюры не отнимают пул у страницы.
constexpr int kThumbPriority = 50'000;
constexpr int kMaxImages = 240;     // миниатюр в памяти (160 × 226 × 4 ≈ 145 КБ каждая)
constexpr double kMaxAspect = 1.6;  // очень высокая страница — ниже, а не шире

}  // namespace

// ---------------------------------------------------------------- модель

class ThumbnailsModel : public QAbstractListModel {
public:
    explicit ThumbnailsModel(ThumbnailsPane& pane) : QAbstractListModel(&pane), pane_(pane) {}

    void reset(int count) {
        beginResetModel();
        count_ = count;
        endResetModel();
    }
    void relabel() {
        if (count_ > 0) {
            emit dataChanged(index(0), index(count_ - 1), {Qt::DisplayRole, Qt::AccessibleTextRole});
        }
    }

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : count_; }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() >= count_) {
            return {};
        }
        if (role == Qt::DisplayRole) {
            return pane_.pageLabel(index.row());
        }
        if (role == Qt::AccessibleTextRole || role == Qt::ToolTipRole) {
            return ThumbnailsPane::tr("Страница %1").arg(pane_.pageLabel(index.row()));
        }
        return {};
    }

    Qt::ItemFlags flags(const QModelIndex& index) const override {
        return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled
                               : Qt::NoItemFlags;
    }

    QStringList mimeTypes() const override { return {QLatin1String(ThumbnailsPane::kMimeType)}; }

    // Перетаскивание страниц (приём — «Упорядочить страницы» 7.1): {"doc": id сессии, "pages": [0…]}.
    QMimeData* mimeData(const QModelIndexList& indexes) const override {
        QList<int> pages;
        for (const QModelIndex& i : indexes) {
            pages << i.row();
        }
        std::sort(pages.begin(), pages.end());
        QJsonArray array;
        for (const int p : pages) {
            array.append(p);
        }
        QJsonObject o;
        o.insert(QStringLiteral("doc"),
                 QString::number(pane_.document() != nullptr ? pane_.document()->id() : 0));
        o.insert(QStringLiteral("pages"), array);
        auto* mime = new QMimeData;
        mime->setData(QLatin1String(ThumbnailsPane::kMimeType),
                      QJsonDocument(o).toJson(QJsonDocument::Compact));
        return mime;
    }

    Qt::DropActions supportedDragActions() const override { return Qt::CopyAction | Qt::MoveAction; }

private:
    ThumbnailsPane& pane_;
    int count_ = 0;
};

// ---------------------------------------------------------------- делегат

class ThumbnailsDelegate : public QStyledItemDelegate {
public:
    explicit ThumbnailsDelegate(ThumbnailsPane& pane) : QStyledItemDelegate(&pane), pane_(pane) {}

    static int labelHeight(const QFontMetrics& fm) { return fm.height() + Theme::sizes().grid; }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const Metrics& m = Theme::sizes();
        const QSizeF t = pane_.thumbSize(index.row());
        const int h = m.grid * 2 + static_cast<int>(std::ceil(t.height())) + m.grid +
                      labelHeight(option.fontMetrics) + m.grid;
        return {std::max(1, pane_.list()->viewport()->width()), h};
    }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const Metrics& m = Theme::sizes();
        const Palette& c = Theme::colors();
        const int page = index.row();
        const QRect r = option.rect;
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        const bool selected = (option.state & QStyle::State_Selected) != 0;
        const bool hover = (option.state & QStyle::State_MouseOver) != 0;
        if (selected || hover) {
            p->setPen(Qt::NoPen);
            p->setBrush(selected ? c.accentSoft.color() : c.hover.color());
            p->drawRoundedRect(QRectF(r).adjusted(m.grid, m.grid / 2.0, -m.grid, -m.grid / 2.0), m.radius,
                               m.radius);
        }
        p->setRenderHint(QPainter::Antialiasing, false);
        const QSizeF t = pane_.thumbSize(page);
        const QRect thumb(r.x() + (r.width() - qRound(t.width())) / 2, r.y() + m.grid * 2, qRound(t.width()),
                          qRound(t.height()));
        const QImage image = pane_.thumbnail(page);
        if (image.isNull()) {
            p->fillRect(thumb, kPaperColor.color());
            const_cast<ThumbnailsPane&>(pane_).request(page);  // рисуется — значит видна
        } else {
            p->setRenderHint(QPainter::SmoothPixmapTransform, true);
            p->drawImage(thumb, image);
        }
        // Рамка листа; текущая страница — accent 2 px снаружи.
        p->setPen(QPen(c.border.color(), 1));
        p->setBrush(Qt::NoBrush);
        p->drawRect(thumb.adjusted(0, 0, -1, -1));
        if (page == pane_.currentPage()) {
            const int w = m.focusRing;
            p->setPen(QPen(c.accent.color(), w));
            p->drawRect(QRectF(thumb).adjusted(-w / 2.0 - 1, -w / 2.0 - 1, w / 2.0 + 1, w / 2.0 + 1));
        }
        // Область просмотра — рамка accent на видимой части страницы (её можно тащить).
        const QRectF f = pane_.visibleFrame(page);
        if (!f.isEmpty()) {
            const QRectF frame(thumb.x() + f.x() * thumb.width(), thumb.y() + f.y() * thumb.height(),
                               f.width() * thumb.width(), f.height() * thumb.height());
            p->setPen(QPen(c.danger.color(), 2));
            p->drawRect(frame.adjusted(1, 1, -1, -1));
        }
        const QRect label(r.x(), thumb.bottom() + m.grid, r.width(), labelHeight(option.fontMetrics));
        p->setPen(selected ? c.text.color() : c.textMuted.color());
        p->setFont(option.font);
        p->drawText(label, Qt::AlignHCenter | Qt::AlignTop, index.data(Qt::DisplayRole).toString());
        p->restore();
    }

private:
    const ThumbnailsPane& pane_;
};

// ---------------------------------------------------------------- панель

ThumbnailsPane::ThumbnailsPane(QWidget* parent) : NavPage(parent) {
    setObjectName(QStringLiteral("thumbnailsPane"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    list_ = new QListView(this);
    list_->setObjectName(QStringLiteral("thumbnailList"));
    list_->setFrameShape(QFrame::NoFrame);
    list_->setUniformItemSizes(false);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setDragEnabled(true);
    list_->setDragDropMode(QAbstractItemView::DragOnly);
    list_->setDefaultDropAction(Qt::MoveAction);
    list_->setResizeMode(QListView::Adjust);
    list_->setLayoutMode(QListView::Batched);
    list_->setBatchSize(200);
    list_->setMouseTracking(true);
    list_->setAccessibleName(tr("Миниатюры страниц"));
    model_ = new ThumbnailsModel(*this);
    delegate_ = new ThumbnailsDelegate(*this);
    list_->setModel(model_);
    list_->setItemDelegate(delegate_);
    list_->viewport()->installEventFilter(this);
    column->addWidget(list_);

    connect(list_, &QListView::clicked, this, [this](const QModelIndex& index) {
        if (view_ != nullptr && index.isValid()) {
            view_->view().goToPage(index.row());
        }
    });
    connect(list_, &QListView::activated, this, [this](const QModelIndex& index) {
        if (view_ != nullptr && index.isValid()) {
            view_->view().goToPage(index.row());
        }
    });
    connect(list_->verticalScrollBar(), &QScrollBar::valueChanged, this, &ThumbnailsPane::cancelInvisible);
    connect(&Theme::instance(), &Theme::themeChanged, this, [this] {
        list_->doItemsLayout();
        list_->viewport()->update();
    });
}

ThumbnailsPane::~ThumbnailsPane() {
    if (RenderPool* p = pool()) {
        for (const quint64 job : std::as_const(jobs_)) {
            p->cancel(job);
        }
    }
}

QString ThumbnailsPane::title() const { return tr("Миниатюры страниц"); }

RenderPool* ThumbnailsPane::pool() {
    if (view_ != nullptr && view_->pool() != nullptr) {
        return view_->pool();
    }
    if (!ownPool_) {
        ownPool_ = std::make_unique<RenderPool>(2);
    }
    return ownPool_.get();
}

void ThumbnailsPane::setDocument(DocumentView* view) {
    if (view == view_ && !dirty_) {
        return;
    }
    if (view_ != nullptr) {
        disconnect(&view_->session(), nullptr, this, nullptr);
        disconnect(&view_->view(), nullptr, this, nullptr);
        disconnect(view_->view().verticalScrollBar(), nullptr, this, nullptr);
        disconnect(view_->view().horizontalScrollBar(), nullptr, this, nullptr);
    }
    resetAll();
    view_ = view;
    labels_.clear();
    dirty_ = true;
    if (isVisible()) {
        rebind();
    } else {
        model_->reset(0);
    }
}

void ThumbnailsPane::activated() {
    if (dirty_) {
        rebind();
    }
    updateFrames();
}

void ThumbnailsPane::rebind() {
    dirty_ = false;
    if (view_ == nullptr) {
        model_->reset(0);
        return;
    }
    DocumentSession& session = view_->session();
    PageView& v = view_->view();
    connect(&session, &DocumentSession::pageChanged, this, &ThumbnailsPane::dropPage, Qt::UniqueConnection);
    connect(&session, &DocumentSession::structureChanged, this, [this] {
        resetAll();
        if (view_ != nullptr) {
            model_->reset(view_->view().pageCount());
        }
    });
    connect(&session, &DocumentSession::layersChanged, this, [this] {
        const int count = model_->rowCount();
        resetAll();
        model_->reset(count);
        syncCurrent(view_ != nullptr ? view_->view().currentPage() : -1);
    });
    connect(&v, &PageView::currentPageChanged, this, &ThumbnailsPane::syncCurrent);
    const auto queueFrames = [this] {
        if (!framesQueued_) {
            framesQueued_ = true;
            QTimer::singleShot(0, this, [this] {
                framesQueued_ = false;
                updateFrames();
            });
        }
    };
    connect(&v, &PageView::zoomChanged, this, queueFrames);
    connect(&v, &PageView::layoutChanged, this, queueFrames);
    connect(v.verticalScrollBar(), &QScrollBar::valueChanged, this, queueFrames);
    connect(v.horizontalScrollBar(), &QScrollBar::valueChanged, this, queueFrames);
    model_->reset(v.pageCount());
    syncCurrent(v.currentPage());
    updateFrames();
}

void ThumbnailsPane::resetAll() {
    ++generation_;
    if (RenderPool* p = view_ != nullptr ? pool() : ownPool_.get()) {
        for (const quint64 job : std::as_const(jobs_)) {
            p->cancel(job);
        }
    }
    jobs_.clear();
    images_.clear();
    pending_.clear();
    failed_.clear();
    frames_.clear();
    current_ = -1;
}

void ThumbnailsPane::dropPage(int page) {
    images_.remove(page);
    failed_.remove(page);
    pending_.remove(page);
    if (const auto it = jobs_.find(page); it != jobs_.end()) {
        pool()->cancel(*it);
        jobs_.erase(it);
    }
    list_->viewport()->update();
}

void ThumbnailsPane::setPageLabels(const QStringList& labels) {
    labels_ = labels;
    model_->relabel();
}

QString ThumbnailsPane::pageLabel(int page) const {
    return page >= 0 && page < labels_.size() && !labels_[page].isEmpty() ? labels_[page]
                                                                          : QString::number(page + 1);
}

QSizeF ThumbnailsPane::thumbSize(int page) const {
    const double width = Theme::sizes().thumbWidth;
    QSizeF pt = view_ != nullptr ? view_->view().pageSizePoints(page) : QSizeF();
    if (pt.isEmpty()) {
        pt = QSizeF(612, 792);
    }
    double w = width;
    double h = width * pt.height() / pt.width();
    if (h > width * kMaxAspect) {
        h = width * kMaxAspect;
        w = h * pt.width() / pt.height();
    }
    return {std::round(w), std::round(h)};
}

QRect ThumbnailsPane::thumbRect(int page) const {
    const QModelIndex index = model_->index(page);
    const QRect item = list_->visualRect(index);
    if (!item.isValid() || !item.intersects(list_->viewport()->rect())) {
        return {};
    }
    const QSizeF t = thumbSize(page);
    return {item.x() + (item.width() - qRound(t.width())) / 2, item.y() + Theme::sizes().grid * 2,
            qRound(t.width()), qRound(t.height())};
}

void ThumbnailsPane::request(int page) {
    if (view_ == nullptr || dirty_ || images_.contains(page) || pending_.contains(page) ||
        failed_.contains(page) || page < 0 || page >= view_->view().pageCount()) {
        return;
    }
    pending_.insert(page);
    requested_.append(page);
    const quint64 generation = generation_;
    view_->session()
        .displayList(page, true, TaskPriority::Low)
        .then(this, [this, page, generation](QFuture<std::shared_ptr<const DisplayList>> done) {
            if (generation != generation_ || !pending_.contains(page)) {
                return;  // документ или слои сменились, строка ушла из вида
            }
            std::shared_ptr<const DisplayList> list;
            try {
                list = done.result();
            } catch (const std::exception&) {
                pending_.remove(page);
                failed_.insert(page);
                return;
            }
            if (!list || view_ == nullptr) {
                pending_.remove(page);
                return;
            }
            const RectF b = list->bounds();
            const QSizeF t = thumbSize(page);
            const float zoom = b.width() > 0 ? static_cast<float>(t.width() / b.width()) : 1.0f;
            const auto dpr = static_cast<float>(devicePixelRatioF());
            const IRect device = pageDeviceRect(b, zoom, dpr, 0);
            DocumentSession& session = view_->session();
            TileRequest request;
            request.key = makeTileKey(session.id(), page, zoom, dpr, 0, TileRect{-2, -2, device},
                                      session.revision(), session.layersHash());
            request.list = std::move(list);
            request.matrix = pageMatrix(b, zoom, dpr, 0);
            request.rect = device;
            request.dpr = dpr;
            request.priority = kThumbPriority;
            jobs_[page] = pool()->submit(request, this, [this, page, generation](const TileResult& result) {
                if (generation != generation_) {
                    return;
                }
                jobs_.remove(page);
                pending_.remove(page);
                if (result.ok()) {
                    images_.insert(page, result.image);
                    if (images_.size() > kMaxImages) {
                        // Лишние — дальние от текущей страницы.
                        QList<int> pages = images_.keys();
                        std::sort(pages.begin(), pages.end(), [this](int a, int b2) {
                            return std::abs(a - current_) > std::abs(b2 - current_);
                        });
                        for (int i = 0; i < images_.size() - kMaxImages; ++i) {
                            images_.remove(pages[i]);
                        }
                    }
                    list_->viewport()->update();
                } else if (!result.aborted) {
                    failed_.insert(page);
                }
            });
        });
}

// Прокрутили список: растры строк, ушедших из вида, не нужны.
void ThumbnailsPane::cancelInvisible() {
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        if (thumbRect(it.key()).isEmpty()) {
            pool()->cancel(it.value());
            pending_.remove(it.key());
            it = jobs_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (!jobs_.contains(*it) && thumbRect(*it).isEmpty()) {
            it = pending_.erase(it);  // display list ещё в пути: ответ будет пропущен
        } else {
            ++it;
        }
    }
}

void ThumbnailsPane::syncCurrent(int page) {
    if (page == current_ || page < 0 || page >= model_->rowCount()) {
        return;
    }
    current_ = page;
    const QModelIndex index = model_->index(page);
    list_->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    if (isVisible()) {
        list_->scrollTo(index, QAbstractItemView::EnsureVisible);
    }
    list_->viewport()->update();
}

void ThumbnailsPane::updateFrames() {
    frames_.clear();
    if (view_ != nullptr && view_->view().viewRotation() == 0) {
        PageView& v = view_->view();
        const QRectF viewport(v.viewport()->rect());
        for (const int page : v.visiblePages(0)) {
            const QRectF pr = v.pageViewportRect(page);
            const QRectF seen = pr.intersected(viewport);
            if (pr.isEmpty() || seen.isEmpty()) {
                continue;
            }
            frames_.insert(page, QRectF((seen.x() - pr.x()) / pr.width(), (seen.y() - pr.y()) / pr.height(),
                                        seen.width() / pr.width(), seen.height() / pr.height()));
        }
    }
    list_->viewport()->update();
}

// Рамка области просмотра тянется мышью — вид прокручивается (view-10).
bool ThumbnailsPane::eventFilter(QObject* watched, QEvent* event) {
    if (watched != list_->viewport() || view_ == nullptr) {
        return NavPage::eventFilter(watched, event);
    }
    const auto frameAt = [this](const QPoint& pos) -> int {
        const QModelIndex index = list_->indexAt(pos);
        if (!index.isValid()) {
            return -1;
        }
        const int page = index.row();
        const QRectF f = frames_.value(page);
        const QRect thumb = thumbRect(page);
        if (f.isEmpty() || thumb.isEmpty()) {
            return -1;
        }
        const QRectF frame(thumb.x() + f.x() * thumb.width(), thumb.y() + f.y() * thumb.height(),
                           f.width() * thumb.width(), f.height() * thumb.height());
        return frame.contains(pos) ? page : -1;
    };
    switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* e = static_cast<QMouseEvent*>(event);
            const int page = e->button() == Qt::LeftButton ? frameAt(e->position().toPoint()) : -1;
            if (page >= 0) {
                dragPage_ = page;
                dragLast_ = e->position();
                list_->viewport()->setCursor(Qt::ClosedHandCursor);
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            auto* e = static_cast<QMouseEvent*>(event);
            if (dragPage_ >= 0) {
                const QRect thumb = thumbRect(dragPage_);
                const QRectF pr = view_->view().pageViewportRect(dragPage_);
                if (!thumb.isEmpty() && !pr.isEmpty()) {
                    const QPointF d = e->position() - dragLast_;
                    PageView& v = view_->view();
                    v.horizontalScrollBar()->setValue(v.horizontalScrollBar()->value() +
                                                      qRound(d.x() * pr.width() / thumb.width()));
                    v.verticalScrollBar()->setValue(v.verticalScrollBar()->value() +
                                                    qRound(d.y() * pr.height() / thumb.height()));
                }
                dragLast_ = e->position();
                return true;
            }
            if (frameAt(e->position().toPoint()) >= 0) {
                list_->viewport()->setCursor(Qt::OpenHandCursor);
            } else {
                list_->viewport()->unsetCursor();
            }
            break;
        }
        case QEvent::MouseButtonRelease:
            if (dragPage_ >= 0) {
                dragPage_ = -1;
                list_->viewport()->setCursor(Qt::OpenHandCursor);
                return true;
            }
            break;
        default:
            break;
    }
    return NavPage::eventFilter(watched, event);
}

void ThumbnailsPane::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        list_->setAccessibleName(tr("Миниатюры страниц"));
    }
    NavPage::changeEvent(event);
}

}  // namespace ot::ui
