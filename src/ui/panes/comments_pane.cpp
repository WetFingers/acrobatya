#include "ui/panes/comments_pane.h"

#include <QAbstractItemModel>
#include <QAction>
#include <QActionGroup>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

#include "core/annot/annots.h"
#include "services/comment_settings.h"
#include "services/measure_ops.h"
#include "services/settings.h"
#include "ui/dialogs/annot_props_dialog.h"
#include "ui/dialogs/summary_dialog.h"
#include "ui/panes/comment_actions.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/comment/popup_widget.h"
#include "ui/tools/comment/reply_editor.h"
#include "ui/tools/measure/measure_mode.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/empty_state.h"
#include "ui/widgets/search_field.h"
#include "ui/widgets/toast.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

namespace comments_pane {

using annot::AnnotType;
using annot::Comment;

QString refKey(const annot::AnnotRef& ref) { return QStringLiteral("%1:%2").arg(ref.page).arg(ref.objnum); }

using comment_actions::dateText;
using comment_actions::stateTitle;
using comment_actions::typeIcon;
using comment_actions::typeTitle;

QString oneLine(const std::string& text) {
    QString s = QString::fromStdString(text);
    s.replace(QLatin1Char('\r'), QLatin1Char(' '));
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return s.simplified();
}

// Модель над деревом панели: корни — показанные (порядок сортировки), дети — ответы. Указатели — в
// CommentTree панели (живут до следующей перестройки, которая сбрасывает модель).
class Model : public QAbstractItemModel {
public:
    using QAbstractItemModel::QAbstractItemModel;

    void reset(const std::vector<const Comment*>& roots) {
        beginResetModel();
        roots_ = roots;
        parent_.clear();
        row_.clear();
        for (int i = 0; i < static_cast<int>(roots_.size()); ++i) {
            row_.insert(roots_[static_cast<std::size_t>(i)], i);
            map(roots_[static_cast<std::size_t>(i)]);
        }
        endResetModel();
    }

    static const Comment* at(const QModelIndex& i) {
        return i.isValid() ? static_cast<const Comment*>(i.internalPointer()) : nullptr;
    }

    QModelIndex indexOf(const Comment* c) const {
        const auto it = row_.constFind(c);
        return it == row_.constEnd() ? QModelIndex() : createIndex(*it, 0, const_cast<Comment*>(c));
    }

    QModelIndex index(int row, int column, const QModelIndex& parent) const override {
        if (column != 0 || row < 0) {
            return {};
        }
        if (!parent.isValid()) {
            if (row >= static_cast<int>(roots_.size())) {
                return {};
            }
            return createIndex(row, 0, const_cast<Comment*>(roots_[static_cast<std::size_t>(row)]));
        }
        const Comment* p = at(parent);
        if (p == nullptr || row >= static_cast<int>(p->replies.size())) {
            return {};
        }
        return createIndex(row, 0, const_cast<Comment*>(&p->replies[static_cast<std::size_t>(row)]));
    }

    QModelIndex parent(const QModelIndex& child) const override {
        const Comment* c = at(child);
        if (c == nullptr) {
            return {};
        }
        const Comment* p = parent_.value(c, nullptr);
        return p == nullptr ? QModelIndex() : indexOf(p);
    }

    int rowCount(const QModelIndex& parent) const override {
        if (!parent.isValid()) {
            return static_cast<int>(roots_.size());
        }
        if (parent.column() != 0) {
            return 0;
        }
        const Comment* p = at(parent);
        return p == nullptr ? 0 : static_cast<int>(p->replies.size());
    }

    int columnCount(const QModelIndex&) const override { return 1; }

    QVariant data(const QModelIndex& index, int role) const override {
        const Comment* c = at(index);
        if (c == nullptr) {
            return {};
        }
        switch (role) {
            case Qt::DisplayRole:
                return QString::fromStdString(c->contents);
            case Qt::ToolTipRole:
                return QString::fromStdString(c->contents).left(500);
            case Qt::AccessibleTextRole:
                return QStringLiteral("%1, %2: %3")
                    .arg(typeTitle(*c), QString::fromStdString(c->author), oneLine(c->contents));
            default:
                return {};
        }
    }

private:
    void map(const Comment* c) {
        for (int i = 0; i < static_cast<int>(c->replies.size()); ++i) {
            const Comment* r = &c->replies[static_cast<std::size_t>(i)];
            parent_.insert(r, c);
            row_.insert(r, i);
            map(r);
        }
    }

    std::vector<const Comment*> roots_;
    QHash<const Comment*, const Comment*> parent_;
    QHash<const Comment*, int> row_;
};

// Карточка 64 px: кружок типа цветом комментария (точка accent — непрочитан), автор и дата, текст, строка
// «стр. · тип · статус · ответы».
class Delegate : public QStyledItemDelegate {
public:
    Delegate(CommentsPane* pane, QObject* parent) : QStyledItemDelegate(parent), pane_(pane) {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override {
        return {option.rect.width(), Theme::sizes().cardRow};
    }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const Comment* c = Model::at(index);
        if (c == nullptr) {
            return;
        }
        const Palette& pal = Theme::colors();
        const Metrics& m = Theme::sizes();
        const bool selected = (option.state & QStyle::State_Selected) != 0;
        const bool hover = (option.state & QStyle::State_MouseOver) != 0;
        const RowColors rc =
            rowColors(pal, selected ? RowState::Selected : (hover ? RowState::Hover : RowState::Normal));
        p->save();
        const QRect r = option.rect;
        if (rc.background.isValid() && rc.background.alpha() > 0) {
            p->fillRect(r, rc.background);
        }
        p->setPen(pal.border.color());
        p->drawLine(r.bottomLeft(), r.bottomRight());

        const int pad = m.grid * 2;
        const int icon = m.iconRow;
        const int badge = icon + m.grid;
        const QRect badgeRect(r.left() + pad, r.top() + pad, badge, badge);
        QColor fill = c->color.empty() ? pal.hover.color() : QColor::fromRgb(c->color.toRgb());
        fill.setAlpha(c->color.empty() ? 255 : 110);
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setPen(Qt::NoPen);
        p->setBrush(fill);
        p->drawEllipse(badgeRect);
        const qreal dpr = p->device() != nullptr ? p->device()->devicePixelRatioF() : 1.0;
        const QPixmap pm = Icons::get(typeIcon(*c), rc.text, icon, dpr);
        p->drawPixmap(
            QRect(badgeRect.left() + (badge - icon) / 2, badgeRect.top() + (badge - icon) / 2, icon, icon),
            pm);
        const bool unread = pane_->isUnread(*c);
        if (unread) {
            const int dot = m.grid + m.grid / 2;
            p->setBrush(pal.accent.color());
            p->drawEllipse(QRect(badgeRect.right() - dot / 2, badgeRect.top() - dot / 4, dot, dot));
        }

        const int x = badgeRect.right() + m.grid * 2;
        const int w = std::max(0, r.right() - pad - x);
        QFont base = option.font;
        const QFontMetrics fm(base);
        const int lh = fm.height();
        int y = r.top() + (m.cardRow - 3 * lh) / 2;
        // Строка 1: автор (непрочитан — жирный) и дата.
        const QString date = dateText(c->date());
        const int dateW = date.isEmpty() ? 0 : fm.horizontalAdvance(date) + m.grid * 2;
        QFont authorFont = base;
        authorFont.setBold(unread);
        p->setFont(authorFont);
        p->setPen(rc.text);
        QString author = QString::fromStdString(c->author);
        if (author.isEmpty()) {
            author = QCoreApplication::translate("ot::ui::CommentsPane", "Без автора");
        }
        p->drawText(QRect(x, y, std::max(0, w - dateW), lh), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(authorFont).elidedText(author, Qt::ElideRight, std::max(0, w - dateW)));
        p->setFont(base);
        p->setPen(rc.detail);
        p->drawText(QRect(x, y, w, lh), Qt::AlignRight | Qt::AlignVCenter, date);
        // Строка 2: текст.
        y += lh;
        QString text = oneLine(c->contents);
        p->setPen(text.isEmpty() ? rc.detail : rc.text);
        if (text.isEmpty()) {
            text = typeTitle(*c);
        }
        p->drawText(QRect(x, y, w, lh), Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(text, Qt::ElideRight, w));
        // Строка 3: страница, тип, статус, ответы, галочка.
        y += lh;
        QStringList meta;
        meta << QCoreApplication::translate("ot::ui::CommentsPane", "Стр. %1")
                    .arg(pane_->pageLabel(c->ref.page));
        if (c->depth == 0) {
            meta << typeTitle(*c);
        }
        if (c->review() != annot::ReviewState::None) {
            meta << stateTitle(c->review());
        }
        if (const int n = c->replyCount(); n > 0 && c->depth == 0) {
            meta << QCoreApplication::translate("ot::ui::CommentsPane", "ответов: %1").arg(n);
        }
        if (c->marked()) {
            meta << QStringLiteral("✓");
        }
        p->setPen(rc.detail);
        p->drawText(QRect(x, y, w, lh), Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(meta.join(QStringLiteral(" · ")), Qt::ElideRight, w));
        p->restore();
    }

private:
    CommentsPane* pane_;
};

// Список карточек: у корней нет колонки ветки (карточка во всю ширину), отступ ответа — фоном своей
// карточки (выбранная, наведение) и вертикальной линией ветки; разворот — двойной щелчок и меню «…».
class CardTree : public QTreeView {
public:
    using QTreeView::QTreeView;

protected:
    void drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const override {
        const bool selected = selectionModel() != nullptr && selectionModel()->isSelected(index);
        const bool hover =
            viewport()->underMouse() && indexAt(viewport()->mapFromGlobal(QCursor::pos())) == index;
        const RowColors rc = rowColors(
            Theme::colors(), selected ? RowState::Selected : (hover ? RowState::Hover : RowState::Normal));
        if (rc.background.isValid() && rc.background.alpha() > 0) {
            painter->fillRect(rect, rc.background);
        }
        if (index.parent().isValid()) {
            const int x = rect.right() - indentation() / 2;
            painter->setPen(Theme::colors().border.color());
            painter->drawLine(x, rect.top(), x, rect.bottom());
        }
    }
};

// Кнопка-иконка с меню (сортировка, фильтр, «…»).
ToolButton* menuButton(const QString& icon, QWidget* parent, QMenu** menu) {
    auto* b = new ToolButton(icon, QString(), parent);
    b->setPopupMode(QToolButton::InstantPopup);
    *menu = new QMenu(b);
    b->setMenu(*menu);
    return b;
}

}  // namespace comments_pane

using comments_pane::Model;

CommentsPane::CommentsPane(QWidget* parent) : NavPage(parent) {
    setObjectName(QStringLiteral("commentsPane"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    auto* top = new QWidget(this);
    top->setObjectName(QStringLiteral("commentsTop"));
    auto* tl = new QVBoxLayout(top);
    tl->setContentsMargins(m.grid * 2, m.grid * 2, m.grid * 2, m.grid);
    tl->setSpacing(m.grid);
    auto* row = new QHBoxLayout();
    row->setSpacing(m.grid);
    search_ = new SearchField(top);
    search_->setObjectName(QStringLiteral("commentsSearch"));
    connect(search_, &QLineEdit::textChanged, this, [this](const QString& text) {
        searchText_ = text;
        refill();
        emit filterChanged();
    });
    row->addWidget(search_, 1);
    sortButton_ = comments_pane::menuButton(QStringLiteral("arrow-up-down"), top, &sortMenu_);
    sortButton_->setObjectName(QStringLiteral("commentsSort"));
    connect(sortMenu_, &QMenu::aboutToShow, this, &CommentsPane::fillSortMenu);
    filterButton_ = comments_pane::menuButton(QStringLiteral("list-filter"), top, &filterMenu_);
    filterButton_->setObjectName(QStringLiteral("commentsFilter"));
    filterButton_->setCheckable(true);
    connect(filterMenu_, &QMenu::aboutToShow, this, &CommentsPane::fillFilterMenu);
    moreButton_ = comments_pane::menuButton(QStringLiteral("ellipsis"), top, &moreMenu_);
    moreButton_->setObjectName(QStringLiteral("commentsMore"));
    connect(moreMenu_, &QMenu::aboutToShow, this, &CommentsPane::fillMoreMenu);
    row->addWidget(sortButton_);
    row->addWidget(filterButton_);
    row->addWidget(moreButton_);
    tl->addLayout(row);
    auto* summaryRow = new QHBoxLayout();
    summaryRow->setSpacing(m.grid);
    summary_ = new QLabel(top);
    summary_->setObjectName(QStringLiteral("commentsFilterSummary"));
    clearButton_ = new QPushButton(top);
    clearButton_->setObjectName(QStringLiteral("commentsClearFilter"));
    clearButton_->setFlat(true);
    connect(clearButton_, &QPushButton::clicked, this, &CommentsPane::clearFilter);
    summaryRow->addWidget(summary_, 1);
    summaryRow->addWidget(clearButton_);
    tl->addLayout(summaryRow);
    column->addWidget(top);

    stack_ = new QStackedWidget(this);
    list_ = new comments_pane::CardTree(stack_);
    list_->setObjectName(QStringLiteral("commentsList"));
    list_->setFrameShape(QFrame::NoFrame);
    list_->setHeaderHidden(true);
    list_->setUniformRowHeights(true);  // виртуализация: 5 000 строк — без пересчёта высот
    list_->setRootIsDecorated(false);
    list_->setExpandsOnDoubleClick(true);
    list_->setIndentation(m.grid * 4);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setMouseTracking(true);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    model_ = new Model(list_);
    delegate_ = new comments_pane::Delegate(this, list_);
    list_->setModel(model_);
    list_->setItemDelegate(delegate_);
    connect(list_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &CommentsPane::currentChanged);
    connect(list_, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        if (const annot::Comment* c = Model::at(index)) {
            selectComment(c->ref, true);
        }
    });
    connect(list_, &QTreeView::customContextMenuRequested, this, &CommentsPane::showRecordMenu);
    empty_ = new EmptyState(QStringLiteral("message-square"), QString(), QString(), stack_);
    empty_->setObjectName(QStringLiteral("commentsEmpty"));
    connect(empty_, &EmptyState::actionTriggered, this, &CommentsPane::clearFilter);
    stack_->addWidget(list_);
    stack_->addWidget(empty_);
    column->addWidget(stack_, 1);
    reply_ = new ReplyEditor(this);
    reply_->hide();
    connect(reply_, &ReplyEditor::submitted, this, &CommentsPane::submitReply);
    connect(reply_, &ReplyEditor::cancelled, this, &CommentsPane::closeReply);
    column->addWidget(reply_);

    reloadTimer_ = new QTimer(this);
    reloadTimer_->setSingleShot(true);
    reloadTimer_->setInterval(0);
    connect(reloadTimer_, &QTimer::timeout, this, &CommentsPane::reload);
    connect(&Theme::instance(), &Theme::themeChanged, list_->viewport(), qOverload<>(&QWidget::update));
    retranslate();
    updateSummary();
}

CommentsPane::~CommentsPane() = default;

QString CommentsPane::title() const { return tr("Комментарии"); }

bool CommentsPane::isEmptyShown() const { return stack_->currentWidget() == empty_; }

void CommentsPane::setDocument(DocumentView* view) {
    for (const QMetaObject::Connection& c : connections_) {
        disconnect(c);
    }
    connections_.clear();
    if (view_ != nullptr && view_ != view) {
        view_->setFilteredAnnots({});
    }
    view_ = view;
    ++generation_;
    loaded_ = false;
    pendingReply_.reset();
    closeReply();
    tree_ = {};
    shown_.clear();
    model_->reset({});
    firstFill_ = true;
    if (view_ != nullptr) {
        DocumentSession& session = view_->session();
        connections_.push_back(
            connect(&session, &DocumentSession::undoStateChanged, this, &CommentsPane::scheduleReload));
        connections_.push_back(
            connect(&session, &DocumentSession::documentChanged, this, &CommentsPane::scheduleReload));
        connections_.push_back(connect(&view_->view().tools(), &ToolController::annotSelectionChanged, this,
                                       &CommentsPane::syncFromPage));
    }
    dirty_ = true;
    if (isVisible()) {
        activated();
    } else {
        refill();
    }
}

void CommentsPane::activated() {
    if (dirty_) {
        reload();
    }
}

void CommentsPane::scheduleReload() {
    dirty_ = true;
    if (isVisible() || filterActive()) {
        reloadTimer_->start();  // фильтр действует и на страницу — дерево нужно и со скрытой панелью
    }
}

void CommentsPane::reload() {
    dirty_ = false;
    if (view_ == nullptr || view_->isViewOnly()) {
        loaded_ = view_ != nullptr;
        refill();
        return;
    }
    const quint64 generation = ++generation_;
    loading_ = true;
    QPointer<CommentsPane> self(this);
    comments::snapshot(view_->session(), TaskPriority::Normal)
        .then(this, [self, generation](QFuture<comments::Snapshot> done) {
            if (self == nullptr) {
                return;
            }
            try {
                self->applySnapshot(done.result(), generation);
            } catch (const std::exception&) {
                if (self->generation_ == generation) {
                    self->applySnapshot({}, generation);  // документ не читается — пусто
                }
            }
        });
}

void CommentsPane::applySnapshot(comments::Snapshot snapshot, quint64 generation) {
    if (generation != generation_ || view_ == nullptr) {
        return;  // вкладку сменили, пока шло чтение
    }
    loading_ = false;
    const QSet<QString> expanded = expandedKeys();
    const std::optional<annot::AnnotRef> current = currentComment();
    labels_ = view_->session().summary().pageLabels;
    read_.setDocument(comments::ReadState::documentKey(snapshot.documentKey, view_->path()));
    tree_ = std::move(snapshot.tree);
    loaded_ = true;
    refill();
    restoreExpanded(expanded, true);
    if (current) {
        syncing_ = true;
        if (const annot::Comment* c = tree_.find(*current)) {
            const QModelIndex index = model_->indexOf(c);
            if (index.isValid()) {
                list_->setCurrentIndex(index);
            }
        }
        syncing_ = false;
    }
    if (replyTarget_ && tree_.find(*replyTarget_) == nullptr) {
        closeReply();  // запись исчезла (undo, удаление)
    }
    if (pendingReply_) {
        const annot::AnnotRef ref = *pendingReply_;
        pendingReply_.reset();
        if (pendingEdit_) {
            startEdit(ref);
        } else {
            startReply(ref);
        }
    }
}

bool CommentsPane::isReplyOpen() const { return !reply_->isHidden(); }

void CommentsPane::startReply(const annot::AnnotRef& ref) {
    if (!loaded_ || tree_.find(ref) == nullptr) {
        pendingReply_ = ref;
        pendingEdit_ = false;
        if (loaded_ && !loading_ && !reloadTimer_->isActive()) {
            pendingReply_.reset();  // дерево свежее, а такой записи нет
        }
        return;
    }
    editing_ = false;
    replyTarget_ = ref;
    openReply();
}

void CommentsPane::startEdit(const annot::AnnotRef& ref) {
    if (!loaded_ || tree_.find(ref) == nullptr) {
        pendingReply_ = ref;
        pendingEdit_ = true;
        if (loaded_ && !loading_ && !reloadTimer_->isActive()) {
            pendingReply_.reset();
        }
        return;
    }
    editing_ = true;
    replyTarget_ = ref;
    openReply();
}

void CommentsPane::openReply() {
    const annot::Comment* c = replyTarget_ ? tree_.find(*replyTarget_) : nullptr;
    if (c == nullptr) {
        closeReply();
        return;
    }
    selectComment(c->ref, false);
    const QString author = c->author.empty() ? tr("без автора") : QString::fromStdString(c->author);
    if (editing_) {
        reply_->setTitle(tr("Изменить текст: %1").arg(author), tr("Сохранить"));
        reply_->setSpans(c->richContents.empty() ? std::vector<annot::RichSpan>{{.text = c->contents}}
                                                 : annot::parseRichContents(c->richContents));
    } else {
        reply_->setTitle(tr("Ответ: %1").arg(author), tr("Ответить"));
        reply_->clear();
    }
    reply_->setEnabled(canAnnotate());
    reply_->show();
    reply_->editor()->setFocus();
}

void CommentsPane::submitReply() {
    if (!replyTarget_ || view_ == nullptr || !canAnnotate() || reply_->isEmpty()) {
        return;
    }
    const annot::AnnotRef target = *replyTarget_;
    if (editing_) {
        comments::editText(view_->session(), target, reply_->plainText(), reply_->richContents());
    } else {
        comments::reply(
            view_->session(), target,
            {.author = me(), .contents = reply_->plainText(), .richContents = reply_->richContents()});
    }
    closeReply();
}

void CommentsPane::closeReply() {
    replyTarget_.reset();
    editing_ = false;
    if (reply_ != nullptr) {
        reply_->clear();
        reply_->hide();
    }
}

annot::CommentFilter CommentsPane::effectiveFilter() const {
    annot::CommentFilter f = filter_;
    f.search = searchText_.trimmed().toStdString();
    f.me = me();
    return f;
}

void CommentsPane::refill() {
    const QSet<QString> expanded = expandedKeys();
    const annot::CommentFilter f = effectiveFilter();
    const auto unread = [this](const annot::Comment& c) { return isUnread(c); };
    shown_ = annot::arrange(tree_, sort_, f, descending_, unread);
    syncing_ = true;
    model_->reset(shown_);
    syncing_ = false;
    restoreExpanded(expanded, firstFill_);
    if (loaded_) {
        firstFill_ = false;
    }
    if (tree_.roots.empty()) {
        empty_->setText(view_ == nullptr ? tr("Нет открытого документа")
                                         : tr("В документе нет комментариев"));
        empty_->setActionText(QString());
        stack_->setCurrentWidget(loaded_ || view_ == nullptr ? static_cast<QWidget*>(empty_) : list_);
    } else if (shown_.empty()) {
        empty_->setText(tr("Нет комментариев, подходящих под фильтр"));
        empty_->setActionText(tr("Сбросить фильтр"));
        stack_->setCurrentWidget(empty_);
    } else {
        stack_->setCurrentWidget(list_);
    }
    updateOverlay();
    updateSummary();
    emit reloaded();
}

void CommentsPane::updateOverlay() {
    if (view_ == nullptr) {
        return;
    }
    const annot::CommentFilter f = effectiveFilter();
    const auto unread = [this](const annot::Comment& c) { return isUnread(c); };
    std::map<int, std::vector<int>> hidden = annot::hiddenByFilter(tree_, f, unread);
    // Открытые окна записок скрытых — закрыть.
    if (!hidden.empty()) {
        PopupLayer& popups = PopupLayer::of(view_->view());
        for (PopupWidget* w : popups.popups()) {
            const annot::AnnotRef ref = w->ref();
            const auto it = hidden.find(ref.page);
            if (it != hidden.end() && std::binary_search(it->second.begin(), it->second.end(), ref.objnum)) {
                popups.close(ref);
            }
        }
    }
    view_->setFilteredAnnots(std::move(hidden));
}

void CommentsPane::updateSummary() {
    const bool active = filterActive();
    filterButton_->setChecked(filter_.active());
    summary_->setVisible(active);
    clearButton_->setVisible(active);
    if (active) {
        summary_->setText(tr("Показано %1 из %2").arg(shown_.size()).arg(tree_.roots.size()));
    }
}

std::vector<annot::AnnotRef> CommentsPane::shownRoots() const {
    std::vector<annot::AnnotRef> out;
    out.reserve(shown_.size());
    for (const annot::Comment* c : shown_) {
        out.push_back(c->ref);
    }
    return out;
}

void CommentsPane::setSort(annot::CommentSort sort, bool descending) {
    if (sort == sort_ && descending == descending_) {
        return;
    }
    sort_ = sort;
    descending_ = descending;
    refill();
}

void CommentsPane::setFilter(annot::CommentFilter filter) {
    filter.search.clear();
    filter_ = std::move(filter);
    refill();
    emit filterChanged();
}

void CommentsPane::clearFilter() {
    filter_ = {};
    if (!searchText_.isEmpty()) {
        const QSignalBlocker block(search_);
        search_->clear();
        searchText_.clear();
    }
    refill();
    emit filterChanged();
}

void CommentsPane::setSearchText(const QString& text) { search_->setText(text); }

void CommentsPane::expandAll() { list_->expandAll(); }

void CommentsPane::collapseAll() { list_->collapseAll(); }

QSet<QString> CommentsPane::expandedKeys() const {
    QSet<QString> keys;
    std::function<void(const QModelIndex&)> walk = [&](const QModelIndex& parent) {
        const int n = model_->rowCount(parent);
        for (int i = 0; i < n; ++i) {
            const QModelIndex index = model_->index(i, 0, parent);
            if (list_->isExpanded(index)) {
                keys.insert(comments_pane::refKey(Model::at(index)->ref));
                walk(index);
            }
        }
    };
    walk({});
    return keys;
}

void CommentsPane::restoreExpanded(const QSet<QString>& keys, bool expandNew) {
    std::function<void(const QModelIndex&)> walk = [&](const QModelIndex& parent) {
        const int n = model_->rowCount(parent);
        for (int i = 0; i < n; ++i) {
            const QModelIndex index = model_->index(i, 0, parent);
            if (model_->rowCount(index) == 0) {
                continue;
            }
            if (expandNew || keys.contains(comments_pane::refKey(Model::at(index)->ref))) {
                list_->setExpanded(index, true);
            }
            walk(index);
        }
    };
    walk({});
}

std::optional<annot::AnnotRef> CommentsPane::currentComment() const {
    const annot::Comment* c = Model::at(list_->currentIndex());
    if (c == nullptr) {
        return std::nullopt;
    }
    return c->ref;
}

void CommentsPane::selectComment(const annot::AnnotRef& ref, bool activate) {
    const annot::Comment* c = tree_.find(ref);
    if (c == nullptr) {
        return;
    }
    const QModelIndex index = model_->indexOf(c);
    if (index.isValid()) {
        syncing_ = true;
        list_->setCurrentIndex(index);
        list_->scrollTo(index);
        syncing_ = false;
    }
    if (!activate || view_ == nullptr) {
        return;
    }
    // Ответ на странице не виден — переход и выделение у его корня.
    const annot::Comment* root = tree_.rootOf(ref);
    const annot::Comment* target = root != nullptr ? root : c;
    PageView& page = view_->view();
    page.ensureVisible(target->ref.page, target->rect, Theme::sizes().grid * 6);
    annot::AnnotInfo info;
    info.ref = target->ref;
    info.type = target->type;
    info.subtype = target->subtype;
    info.rect = target->rect;
    info.flags = target->flags;
    info.author = target->author;
    info.contents = target->contents;
    info.color = target->color;
    syncing_ = true;
    page.tools().annotSelect().setSelection({info});
    syncing_ = false;
    if (isUnread(*c)) {
        setRead(c->ref, true);
    }
    emit currentCommentChanged(c->ref);
}

void CommentsPane::currentChanged() {
    if (syncing_) {
        return;
    }
    if (const annot::Comment* c = Model::at(list_->currentIndex())) {
        selectComment(c->ref, true);  // клавиши в списке — как щелчок
    }
}

void CommentsPane::syncFromPage() {
    if (syncing_ || view_ == nullptr) {
        return;
    }
    const auto& selection = view_->view().tools().annotSelect().selection();
    if (selection.empty()) {
        return;
    }
    const annot::Comment* root = tree_.rootOf(selection.front().ref);
    if (root == nullptr) {
        return;
    }
    const QModelIndex index = model_->indexOf(root);
    if (!index.isValid()) {
        return;
    }
    syncing_ = true;
    list_->setCurrentIndex(index);
    list_->scrollTo(index);
    syncing_ = false;
}

bool CommentsPane::isUnread(const annot::Comment& c) const { return !read_.isRead(c, me()); }

void CommentsPane::setRead(const annot::AnnotRef& ref, bool read) {
    read_.setRead(ref, read);
    list_->viewport()->update();
    if (filter_.unreadOnly) {
        refill();
    }
}

std::string CommentsPane::me() const { return comments::author(Settings()); }

QString CommentsPane::pageLabel(int page) const {
    return page >= 0 && page < labels_.size() && !labels_[page].isEmpty() ? labels_[page]
                                                                          : QString::number(page + 1);
}

bool CommentsPane::canAnnotate() const { return view_ != nullptr && view_->canAnnotate(); }

void CommentsPane::showRecordMenu(const QPoint& pos) {
    const annot::Comment* c = Model::at(list_->indexAt(pos));
    if (c == nullptr) {
        return;
    }
    QMenu menu(this);
    fillRecordMenu(menu, c->ref);
    if (!menu.isEmpty()) {
        menu.exec(list_->viewport()->mapToGlobal(pos));
    }
}

void CommentsPane::fillRecordMenu(QMenu& menu, const annot::AnnotRef& ref) {
    const annot::Comment* c = tree_.find(ref);
    if (c == nullptr) {
        return;
    }
    QPointer<CommentsPane> self(this);
    const bool editable = canAnnotate();
    QAction* replyAction =
        menu.addAction(Icons::icon(QStringLiteral("reply")), tr("&Ответить"), this, [self, ref] {
            if (self != nullptr) {
                self->startReply(ref);
            }
        });
    replyAction->setObjectName(QStringLiteral("commentsReply"));
    replyAction->setEnabled(editable);
    QAction* edit = menu.addAction(tr("&Изменить текст"), this, [self, ref] {
        if (self != nullptr) {
            self->startEdit(ref);
        }
    });
    edit->setObjectName(QStringLiteral("commentsEditText"));
    edit->setEnabled(editable && (c->flags & annot::kAnnotLocked) == 0);
    if (view_ != nullptr) {
        comment_actions::addStatusMenu(menu, view_->session(), ref, c->review(), editable);
        comment_actions::addCheckAction(menu, view_->session(), ref, c->marked(), editable);
    }
    const bool unread = isUnread(*c);
    QAction* readAction =
        menu.addAction(unread ? tr("Отметить как &прочитанное") : tr("Отметить как &непрочитанное"), this,
                       [self, ref, unread] {
                           if (self != nullptr) {
                               self->setRead(ref, unread);
                           }
                       });
    readAction->setObjectName(QStringLiteral("commentsToggleRead"));
    emit recordMenuRequested(&menu, ref);
    menu.addSeparator();
    QAction* remove =
        menu.addAction(Icons::icon(QStringLiteral("trash-2")), tr("&Удалить"), this, [self, ref] {
            if (self != nullptr) {
                self->removeComment(ref);
            }
        });
    remove->setObjectName(QStringLiteral("commentsRemove"));
    remove->setEnabled(canAnnotate() && (c->flags & annot::kAnnotLocked) == 0);
    QAction* props = menu.addAction(tr("&Свойства…"), this, [self, ref] {
        if (self == nullptr || self->view_ == nullptr) {
            return;
        }
        const annot::Comment* target = self->tree_.find(ref);
        if (target != nullptr) {
            AnnotPropsDialog::openFor(self->view_->view(), target->ref);
        }
    });
    props->setObjectName(QStringLiteral("commentsProperties"));
}

void CommentsPane::removeComment(const annot::AnnotRef& ref) {
    const annot::Comment* c = tree_.find(ref);
    if (c == nullptr || view_ == nullptr || !canAnnotate()) {
        return;
    }
    const int replies = c->replyCount();
    if (replies > 0) {
        const QString text = tr("Удалить комментарий вместе с ответами (%1)?").arg(replies);
        const bool ok = confirm_ ? confirm_(text)
                                 : QMessageBox::question(this, tr("Удалить комментарий"), text,
                                                         QMessageBox::Yes | QMessageBox::No,
                                                         QMessageBox::No) == QMessageBox::Yes;
        if (!ok) {
            return;
        }
    }
    comments::removeBranch(view_->session(), ref);  // ошибка — taskFailed сессии
}

// ---------------------------------------------------------------- FDF/XFDF и сводка

void CommentsPane::exportComments(comments::ExchangeFormat format, bool selectedOnly) {
    if (view_ == nullptr) {
        return;
    }
    const bool fdf = format == comments::ExchangeFormat::Fdf;
    const QString ext = fdf ? QStringLiteral("fdf") : QStringLiteral("xfdf");
    QString base = view_->path();
    if (base.isEmpty()) {
        base = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                   .filePath(view_->title());
    }
    const QString suggested = QFileInfo(base).absolutePath() + QLatin1Char('/') +
                              QFileInfo(base).completeBaseName() + QLatin1Char('.') + ext;
    const QString filter = fdf ? tr("Комментарии FDF (*.fdf)") : tr("Комментарии XFDF (*.xfdf)");
    const QString path =
        saveChooser_ ? saveChooser_(suggested, filter)
                     : QFileDialog::getSaveFileName(this, tr("Экспорт комментариев"), suggested, filter);
    if (!path.isEmpty()) {
        exportTo(path, format, selectedOnly);
    }
}

void CommentsPane::exportTo(const QString& path, comments::ExchangeFormat format, bool selectedOnly) {
    if (view_ == nullptr) {
        return;
    }
    std::vector<annot::AnnotRef> refs;
    if (selectedOnly) {
        if (const auto current = currentComment()) {
            refs.push_back(*current);
        }
    } else {
        for (const annot::Comment& c : tree_.roots) {
            refs.push_back(c.ref);  // ветки — ядро (ответы, статусы, части групп)
        }
    }
    if (refs.empty()) {
        Toast::notify(view_, tr("Нет комментариев для экспорта."), Tone::Neutral);
        return;
    }
    QPointer<CommentsPane> self(this);
    comments::exportFile(view_->session(), std::move(refs), format, path)
        .then(this, [self, path](QFuture<void> f) {
            if (self == nullptr) {
                return;
            }
            try {
                f.waitForFinished();
            } catch (const std::exception&) {
                if (self->view_ != nullptr) {
                    Toast::notify(self->view_,
                                  tr("Не удалось записать «%1».").arg(QFileInfo(path).fileName()));
                }
                return;
            }
            if (self->view_ != nullptr) {
                Toast::notify(self->view_,
                              tr("Комментарии сохранены в «%1».").arg(QFileInfo(path).fileName()), Tone::Ok);
            }
            emit self->exported(path);
        });
}

void CommentsPane::exportMeasures() {
    if (view_ == nullptr) {
        return;
    }
    QString base = view_->path();
    if (base.isEmpty()) {
        base = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                   .filePath(view_->title());
    }
    const QString suggested = QFileInfo(base).absolutePath() + QLatin1Char('/') +
                              tr("%1 — измерения.csv").arg(QFileInfo(base).completeBaseName());
    const QString filter = tr("Таблица CSV (*.csv)");
    const QString path = saveChooser_
                             ? saveChooser_(suggested, filter)
                             : QFileDialog::getSaveFileName(this, tr("Экспорт измерений"), suggested, filter);
    if (!path.isEmpty()) {
        exportMeasuresTo(path);
    }
}

void CommentsPane::exportMeasuresTo(const QString& path) {
    if (view_ == nullptr) {
        return;
    }
    QPointer<CommentsPane> self(this);
    measure::list(view_->session()).then(this, [self, path](QFuture<std::vector<measure::Measurement>> f) {
        if (self == nullptr || self->view_ == nullptr) {
            return;
        }
        std::vector<measure::Measurement> all;
        try {
            all = f.result();
        } catch (
            const std::exception&) {  // NOLINT(bugprone-empty-catch): не прочитались — как «нет измерений»
        }
        if (all.empty()) {
            Toast::notify(self->view_, tr("В документе нет измерений."), Tone::Neutral);
            return;
        }
        std::vector<measure::CsvRow> rows;
        for (const measure::Measurement& m : all) {
            measure::CsvRow row;
            row.page = m.ref.page + 1;
            row.kind =
                MeasureMode::actionTitle(m.kind == measure::Kind::Area        ? MeasureAction::Area
                                         : m.kind == measure::Kind::Perimeter ? MeasureAction::Perimeter
                                                                              : MeasureAction::Distance);
            row.value = m.value;
            row.hasValue = m.hasValue;
            row.precision = m.precision;
            const bool area = m.unit.rfind("sq ", 0) == 0;
            const std::optional<measure::Unit> unit = measure::unitFromLabel(m.unit);
            row.unit = unit ? MeasureMode::unitLabel(*unit, area) : QString::fromStdString(m.unit);
            row.caption = QString::fromStdString(m.caption);
            rows.push_back(std::move(row));
        }
        const QStringList header = {tr("Страница"), tr("Тип"), tr("Значение"), tr("Единицы"), tr("Подпись")};
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(measure::toCsv(rows, header, QLocale())) < 0 ||
            !file.commit()) {
            Toast::notify(self->view_, tr("Не удалось записать «%1».").arg(QFileInfo(path).fileName()));
            return;
        }
        Toast::notify(self->view_,
                      tr("Измерений: %1 — сохранено в «%2».")
                          .arg(static_cast<int>(rows.size()))
                          .arg(QFileInfo(path).fileName()),
                      Tone::Ok);
        emit self->measuresExported(path, static_cast<int>(rows.size()));
    });
}

void CommentsPane::importComments() {
    if (view_ == nullptr || !canAnnotate()) {
        return;
    }
    const QString filter = tr("Комментарии FDF и XFDF (*.fdf *.xfdf)");
    const QString path = openChooser_
                             ? openChooser_(filter)
                             : QFileDialog::getOpenFileName(this, tr("Импорт комментариев"),
                                                            QFileInfo(view_->path()).absolutePath(), filter);
    if (!path.isEmpty()) {
        importFrom(path);
    }
}

void CommentsPane::importFrom(const QString& path) {
    if (view_ == nullptr || !canAnnotate()) {
        return;
    }
    const QString name = QFileInfo(path).fileName();
    comments::ExchangeFile file;
    try {
        file = comments::readExchange(path);
    } catch (const std::exception&) {
        const QString reason = tr("«%1» — не файл комментариев FDF или XFDF.").arg(name);
        Toast::notify(view_, reason);
        emit importRefused(reason);
        return;
    }
    // Чужой документ (/F, <f href>) — отказ до вопроса (эталон 7.3).
    const QString own = QFileInfo(view_->path()).fileName();
    if (!annot::sameDocumentName(file.info.file, own.toStdString())) {
        const QString reason =
            tr("Комментарии из «%1» относятся к другому документу («%2») — импорт отменён.")
                .arg(name, QFileInfo(QString::fromStdString(file.info.file)).fileName());
        Toast::notify(view_, reason);
        emit importRefused(reason);
        return;
    }
    const int count = file.info.annots;
    const bool ok =
        importConfirm_
            ? importConfirm_(name, count)
            : QMessageBox::question(
                  this, tr("Импорт комментариев"),
                  tr("Импортировать комментарии из файла «%1»? Объектов: %2.").arg(name).arg(count),
                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    if (!ok) {
        return;
    }
    QPointer<CommentsPane> self(this);
    comments::importFile(view_->session(), std::move(file))
        .then(this, [self, name](QFuture<annot::ImportReport> f) {
            if (self == nullptr) {
                return;
            }
            annot::ImportReport report;
            try {
                report = f.result();
            } catch (const std::exception&) {
                const QString reason = tr("Комментарии из «%1» импортировать не удалось.").arg(name);
                if (self->view_ != nullptr) {
                    Toast::notify(self->view_, reason);
                }
                emit self->importRefused(reason);
                return;
            }
            QString text = tr("Импортировано: %1").arg(report.created);
            if (report.updated > 0) {
                text += tr(", обновлено: %1").arg(report.updated);
            }
            if (report.skipped > 0) {
                QStringList pages;
                for (const int p : report.skippedPages) {
                    pages << QString::number(p + 1);
                }
                text += tr(". Пропущено %1: в документе нет страниц %2")
                            .arg(report.skipped)
                            .arg(pages.join(QStringLiteral(", ")));
            }
            if (self->view_ != nullptr) {
                Toast::notify(self->view_, text + QLatin1Char('.'),
                              report.skipped > 0 ? Tone::Warn : Tone::Ok);
            }
            emit self->imported(report);
        });
}

void CommentsPane::showSummary() {
    auto* window = qobject_cast<MainWindow*>(this->window());
    if (window == nullptr || view_ == nullptr) {
        return;
    }
    SummaryDialog::openFor(*window, *view_, this);
}

void CommentsPane::fillSortMenu() {
    sortMenu_->clear();
    auto* group = new QActionGroup(sortMenu_);
    const std::pair<annot::CommentSort, QString> keys[] = {
        {annot::CommentSort::Page, tr("По &странице")},  {annot::CommentSort::Author, tr("По &автору")},
        {annot::CommentSort::Date, tr("По &дате")},      {annot::CommentSort::Type, tr("По &типу")},
        {annot::CommentSort::Status, tr("По с&татусу")},
    };
    int i = 0;
    for (const auto& [key, text] : keys) {
        QAction* a = sortMenu_->addAction(text);
        a->setObjectName(QStringLiteral("commentsSort%1").arg(i++));
        a->setCheckable(true);
        a->setChecked(sort_ == key);
        group->addAction(a);
        const annot::CommentSort k = key;
        connect(a, &QAction::triggered, this, [this, k] { setSort(k, descending_); });
    }
    sortMenu_->addSeparator();
    QAction* desc = sortMenu_->addAction(tr("По &убыванию"));
    desc->setObjectName(QStringLiteral("commentsSortDescending"));
    desc->setCheckable(true);
    desc->setChecked(descending_);
    connect(desc, &QAction::toggled, this, [this](bool on) { setSort(sort_, on); });
}

void CommentsPane::fillFilterMenu() {
    filterMenu_->clear();
    // Что есть в документе: типы, авторы, цвета.
    std::vector<annot::AnnotType> types;
    QStringList authors;
    std::vector<std::uint32_t> colors;
    for (const annot::Comment& c : tree_.roots) {
        if (std::find(types.begin(), types.end(), c.type) == types.end()) {
            types.push_back(c.type);
        }
        const QString a = QString::fromStdString(c.author);
        if (!authors.contains(a)) {
            authors << a;
        }
        if (!c.color.empty() && std::find(colors.begin(), colors.end(), c.color.toRgb()) == colors.end()) {
            colors.push_back(c.color.toRgb());
        }
    }
    std::sort(types.begin(), types.end());
    authors.sort(Qt::CaseInsensitive);

    QMenu* typeMenu = filterMenu_->addMenu(tr("&Тип"));
    typeMenu->setObjectName(QStringLiteral("commentsFilterTypes"));
    for (const annot::AnnotType t : types) {
        annot::Comment probe;
        probe.type = t;
        QAction* a = typeMenu->addAction(comments_pane::typeTitle(probe));
        a->setCheckable(true);
        const std::uint32_t bit = 1u << static_cast<int>(t);
        a->setChecked((filter_.types & bit) != 0);
        connect(a, &QAction::toggled, this, [this, bit](bool on) {
            annot::CommentFilter f = filter_;
            f.types = on ? (f.types | bit) : (f.types & ~bit);
            setFilter(f);
        });
    }
    QMenu* authorMenu = filterMenu_->addMenu(tr("&Автор"));
    authorMenu->setObjectName(QStringLiteral("commentsFilterAuthors"));
    for (const QString& author : authors) {
        QAction* a = authorMenu->addAction(author.isEmpty() ? tr("Без автора") : author);
        a->setCheckable(true);
        const std::string name = author.toStdString();
        a->setChecked(std::find(filter_.authors.begin(), filter_.authors.end(), name) !=
                      filter_.authors.end());
        connect(a, &QAction::toggled, this, [this, name](bool on) {
            annot::CommentFilter f = filter_;
            std::erase(f.authors, name);
            if (on) {
                f.authors.push_back(name);
            }
            setFilter(f);
        });
    }
    QMenu* stateMenu = filterMenu_->addMenu(tr("&Статус"));
    stateMenu->setObjectName(QStringLiteral("commentsFilterStates"));
    for (int i = 0; i < annot::kReviewStateCount; ++i) {
        const auto state = static_cast<annot::ReviewState>(i);
        QAction* a = stateMenu->addAction(comments_pane::stateTitle(state));
        a->setCheckable(true);
        a->setChecked(std::find(filter_.states.begin(), filter_.states.end(), state) != filter_.states.end());
        connect(a, &QAction::toggled, this, [this, state](bool on) {
            annot::CommentFilter f = filter_;
            std::erase(f.states, state);
            if (on) {
                f.states.push_back(state);
            }
            setFilter(f);
        });
    }
    QMenu* colorMenu = filterMenu_->addMenu(tr("&Цвет"));
    colorMenu->setObjectName(QStringLiteral("commentsFilterColors"));
    const int icon = Theme::sizes().iconRow;
    for (const std::uint32_t rgb : colors) {
        QPixmap swatch(icon, icon);
        swatch.fill(QColor::fromRgb(rgb));
        QAction* a = colorMenu->addAction(QIcon(swatch), QColor::fromRgb(rgb).name(QColor::HexRgb));
        a->setCheckable(true);
        a->setChecked(std::find(filter_.colors.begin(), filter_.colors.end(), rgb) != filter_.colors.end());
        connect(a, &QAction::toggled, this, [this, rgb](bool on) {
            annot::CommentFilter f = filter_;
            std::erase(f.colors, rgb);
            if (on) {
                f.colors.push_back(rgb);
            }
            setFilter(f);
        });
    }
    filterMenu_->addSeparator();
    const auto toggle = [this](const QString& text, const char* name, bool annot::CommentFilter::* field) {
        QAction* a = filterMenu_->addAction(text);
        a->setObjectName(QLatin1String(name));
        a->setCheckable(true);
        a->setChecked(filter_.*field);
        connect(a, &QAction::toggled, this, [this, field](bool on) {
            annot::CommentFilter f = filter_;
            f.*field = on;
            setFilter(f);
        });
    };
    toggle(tr("Только &мои"), "commentsFilterMine", &annot::CommentFilter::onlyMine);
    toggle(tr("Только &непрочитанные"), "commentsFilterUnread", &annot::CommentFilter::unreadOnly);
    toggle(tr("Только с &галочкой"), "commentsFilterMarked", &annot::CommentFilter::markedOnly);
    filterMenu_->addSeparator();
    QAction* clear = filterMenu_->addAction(Icons::icon(QStringLiteral("funnel-x")), tr("Сб&росить фильтр"));
    clear->setObjectName(QStringLiteral("commentsFilterClear"));
    clear->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_8));
    clear->setShortcutVisibleInContextMenu(true);
    clear->setEnabled(filterActive());
    connect(clear, &QAction::triggered, this, &CommentsPane::clearFilter);
}

void CommentsPane::fillMoreMenu() {
    moreMenu_->clear();
    QAction* expand = moreMenu_->addAction(Icons::icon(QStringLiteral("chevrons-up-down")),
                                           tr("&Развернуть все"), this, &CommentsPane::expandAll);
    expand->setObjectName(QStringLiteral("commentsExpandAll"));
    QAction* collapse = moreMenu_->addAction(Icons::icon(QStringLiteral("chevrons-down-up")),
                                             tr("&Свернуть все"), this, &CommentsPane::collapseAll);
    collapse->setObjectName(QStringLiteral("commentsCollapseAll"));
    moreMenu_->addSeparator();
    const bool hasComments = !tree_.roots.empty();
    QMenu* exportAll =
        moreMenu_->addMenu(Icons::icon(QStringLiteral("file-up")), tr("&Экспорт всех комментариев"));
    exportAll->setObjectName(QStringLiteral("commentsExportAll"));
    exportAll->setEnabled(hasComments);
    QMenu* exportOne = moreMenu_->addMenu(tr("Экспорт &выбранного"));
    exportOne->setObjectName(QStringLiteral("commentsExportSelected"));
    exportOne->setEnabled(currentComment().has_value());
    for (QMenu* sub : {exportAll, exportOne}) {
        const bool selected = sub == exportOne;
        QAction* fdf = sub->addAction(tr("В файл &FDF…"), this, [this, selected] {
            exportComments(comments::ExchangeFormat::Fdf, selected);
        });
        fdf->setObjectName(selected ? QStringLiteral("commentsExportSelectedFdf")
                                    : QStringLiteral("commentsExportFdf"));
        QAction* xfdf = sub->addAction(tr("В файл &XFDF…"), this, [this, selected] {
            exportComments(comments::ExchangeFormat::Xfdf, selected);
        });
        xfdf->setObjectName(selected ? QStringLiteral("commentsExportSelectedXfdf")
                                     : QStringLiteral("commentsExportXfdf"));
    }
    QAction* import = moreMenu_->addAction(Icons::icon(QStringLiteral("file-down")),
                                           tr("&Импорт комментариев…"), this, &CommentsPane::importComments);
    import->setObjectName(QStringLiteral("commentsImport"));
    import->setEnabled(canAnnotate());
    moreMenu_->addSeparator();
    QAction* summary = moreMenu_->addAction(Icons::icon(QStringLiteral("list-checks")),
                                            tr("&Сводка комментариев…"), this, &CommentsPane::showSummary);
    summary->setObjectName(QStringLiteral("commentsSummary"));
    summary->setEnabled(view_ != nullptr && !view_->isViewOnly());
    emit moreMenuRequested(moreMenu_);
    moreMenu_->addSeparator();
    QAction* measures =
        moreMenu_->addAction(Icons::icon(QStringLiteral("sheet")), tr("Экспорт &измерений в CSV…"), this,
                             &CommentsPane::exportMeasures);
    measures->setObjectName(QStringLiteral("commentsExportMeasures"));
    measures->setEnabled(view_ != nullptr && !view_->isViewOnly());
    measures->setToolTip(tr("Расстояния, периметры и площади документа — таблицей для Excel"));
}

void CommentsPane::retranslate() {
    search_->setPlaceholderText(tr("Поиск в комментариях"));
    search_->setAccessibleName(tr("Поиск в комментариях"));
    sortButton_->setLabel(tr("Сортировка"));
    filterButton_->setLabel(tr("Фильтр"));
    moreButton_->setLabel(tr("Другие действия"));
    clearButton_->setText(tr("Сбросить"));
    clearButton_->setAccessibleName(tr("Сбросить фильтр"));
    list_->setAccessibleName(tr("Комментарии"));
    updateSummary();
    refill();
}

void CommentsPane::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    NavPage::changeEvent(event);
}

}  // namespace ot::ui
