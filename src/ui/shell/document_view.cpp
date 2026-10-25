#include "ui/shell/document_view.h"

#include <QFileInfo>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>

#include "core/annot/annots.h"
#include "services/settings.h"
#include "ui/panes/find_bar.h"
#include "ui/shell/message_bar.h"
#include "ui/shell/signature_status.h"
#include "ui/theme/theme.h"
#include "ui/tools/comment/annot_menu.h"
#include "ui/tools/comment/popup_widget.h"
#include "ui/tools/comment/quick_actions_bar.h"
#include "ui/view/boxes_overlay.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/message_bar.h"

namespace ot::ui {

DocumentView::DocumentView(std::shared_ptr<DocumentSession> session, RenderPool* pool, TileCache* cache,
                           QWidget* parent)
    : QWidget(parent), session_(std::move(session)), pool_(pool) {
    setObjectName(QStringLiteral("documentView"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    stack_ = new MessageStack(this);
    column->addWidget(stack_);
    view_ = new PageView(this);
    view_->setObjectName(QStringLiteral("pageView"));
    column->addWidget(view_, 1);
    // Раскладка и масштаб по умолчанию (Установки → Просмотр страницы, view-36): «Автоматически» —
    // /PageLayout документа, иначе — выбранная; масштаб — после показа (нужен размер окна), последний вид и
    // /OpenAction файла — важнее.
    const Settings settings;
    const QString layout = settings.get(settings::kDefaultLayout);
    const std::optional<PageMode> mode = defaultLayoutMode(layout);
    if (mode) {
        view_->setRespectDocumentLayout(false);
    }
    defaultZoom_ = settings.get(settings::kDefaultZoom);
    view_->setDocument(session_.get(), pool, cache);
    if (mode) {
        view_->setPageMode(*mode);
    }
    setFocusProxy(view_);
    const OpenSummary summary = session_->summary();
    permissions_ = summary.permissions;
    pdfx_ = summary.pdfx;
    view_->tools().setEditable(canAnnotate());
    view_->tools().setCopyAllowed(permissions_.copy);
    find_ = new FindBar(*session_, *view_, this);
    quickActions_ = new QuickActionsBar(*view_);
    installCommentMenu(*view_);
    find_->installEventFilter(this);
    updateReplaceBlocked();

    connect(session_.get(), &DocumentSession::undoStateChanged, this, &DocumentView::updateModified,
            Qt::QueuedConnection);
    connect(session_.get(), &DocumentSession::saved, this, &DocumentView::updateModified,
            Qt::QueuedConnection);
    connect(
        session_.get(), &DocumentSession::saved, this,
        [this] {
            const OpenSummary summary = session_->summary();
            permissions_ = summary.permissions;
            pdfx_ = summary.pdfx;
            view_->tools().setEditable(canAnnotate());
            view_->tools().setCopyAllowed(permissions_.copy);
            view_->setRenderStyle(viewStyle_.styleFor(pdfx_));
            updateReplaceBlocked();
        },
        Qt::QueuedConnection);
    updateModified();

    signatures_ = std::make_unique<SignatureStatus>(*this);
    if (isViewOnly()) {
        MessageBar* bar = stack_->showMessage(
            MessageKind::ViewOnly,
            tr("Файл открыт только для просмотра. Чтобы править его, сохраните копию в формате PDF."));
        bar->setObjectName(QStringLiteral("viewOnlyBar"));
        bar->addAction(tr("Сохранить как PDF"), QStringLiteral("saveAsPdf"), true);
        connect(bar, &MessageBar::actionTriggered, this, [this](const QString& id) {
            if (id == QLatin1String("saveAsPdf")) {
                emit saveAsPdfRequested();
            }
        });
    }
}

// Вид отпускает документ раньше сессии: задачи тайлов отменяются, кеш документа чистится (TilePainter).
DocumentView::~DocumentView() {
    signatures_.reset();
    view_->setDocument(nullptr);
}

QString DocumentView::path() const { return session_->path(); }

QString DocumentView::title() const { return QFileInfo(session_->path()).fileName(); }

bool DocumentView::isViewOnly() const { return !session_->summary().isPdf; }

void DocumentView::setReadOnly(bool readOnly) {
    if (readOnly_ == readOnly) {
        return;
    }
    readOnly_ = readOnly;
    view_->tools().setEditable(canAnnotate());
    updateReplaceBlocked();
    emit readOnlyChanged(readOnly);
}

std::uint32_t DocumentView::commentTypesMask() {
    std::uint32_t mask = 0;
    for (int i = 0; i < annot::kAnnotTypeCount; ++i) {
        if (annot::isComment(static_cast<annot::AnnotType>(i))) {
            mask |= 1u << i;
        }
    }
    return mask;
}

void DocumentView::setAnnotsHidden(std::uint32_t mask) {
    if (mask == annotsHidden_) {
        return;
    }
    annotsHidden_ = mask;
    view_->tilePainter().setAnnotsHidden(mask);
    view_->tools().annotSelect().setHiddenTypes(mask);
    if (mask != 0) {
        PopupLayer::of(*view_).closeAll();
    }
    emit annotsHiddenChanged(mask);
}

void DocumentView::setFilteredAnnots(std::map<int, std::vector<int>> byPage) {
    for (auto& [page, list] : byPage) {
        std::sort(list.begin(), list.end());
    }
    if (byPage == filtered_) {
        return;
    }
    filtered_ = std::move(byPage);
    view_->tilePainter().setHiddenAnnots(filtered_);
    view_->tools().annotSelect().setHiddenAnnots(filtered_);
    emit filteredAnnotsChanged();
}

void DocumentView::setCommentsHidden(bool hidden) { setAnnotsHidden(hidden ? commentTypesMask() : 0); }

bool DocumentView::commentsHidden() const {
    return (annotsHidden_ & commentTypesMask()) == commentTypesMask();
}

void DocumentView::setViewStyle(const ViewStyle& style) {
    viewStyle_ = style;
    view_->setRenderStyle(style.styleFor(pdfx_));
    view_->boxesOverlay().setShown(style.showPageBoxes);
}

std::optional<PageMode> DocumentView::defaultLayoutMode(const QString& value) {
    const std::pair<const char*, PageMode> modes[] = {{"single", PageMode::SinglePage},
                                                      {"continuous", PageMode::Continuous},
                                                      {"facing", PageMode::Facing},
                                                      {"facingContinuous", PageMode::FacingContinuous}};
    for (const auto& [name, mode] : modes) {
        if (value == QLatin1String(name)) {
            return mode;
        }
    }
    return std::nullopt;
}

void DocumentView::applyDefaultZoom(const QString& value) {
    const std::pair<const char*, FitMode> fits[] = {{"fitPage", FitMode::Page},
                                                    {"fitWidth", FitMode::Width},
                                                    {"fitVisible", FitMode::Visible},
                                                    {"actual", FitMode::ActualSize}};
    for (const auto& [name, fit] : fits) {
        if (value == QLatin1String(name)) {
            view_->fit(fit);
            return;
        }
    }
    bool ok = false;
    const int percent = value.toInt(&ok);
    if (ok && percent > 0) {
        view_->setZoomPercent(static_cast<float>(percent));
    }
}

void DocumentView::setProtectedView(protected_view::Origin origin) {
    if (protected_ == origin) {
        return;
    }
    const bool was = isProtectedView();
    protected_ = origin;
    view_->tools().setEditable(canAnnotate());
    updateReplaceBlocked();
    if (was != isProtectedView()) {
        emit protectedViewChanged(isProtectedView());
    }
}

// «Заменить» панели «Найти» (3.4): только PDF, который можно править; подписанный — правка сломает подпись.
void DocumentView::updateReplaceBlocked() {
    QString reason;
    if (isViewOnly()) {
        reason = tr("Заменять можно только в PDF");
    } else if (isProtectedView()) {
        reason = tr("Защищённый просмотр: правка отключена");
    } else if (readOnly_) {
        reason = tr("Документ открыт только для чтения");
    } else if (!permissions_.allows(Perm::Edit)) {
        reason = tr("Документ защищён: изменение содержимого запрещено");
    } else if (session_->summary().isSigned) {
        reason = tr("Документ подписан: правка сломает подпись");
    }
    find_->setReplaceBlocked(reason);
}

MessageBar* DocumentView::addMessage(Tone tone, const QString& text) { return stack_->add(tone, text); }

QList<MessageBar*> DocumentView::messages() const { return stack_->bars(); }

void DocumentView::setFloatingBar(QWidget* bar) {
    if (floating_ != nullptr && floating_ != bar) {
        floating_->deleteLater();
    }
    floating_ = bar;
    if (bar != nullptr) {
        bar->setParent(this);
        bar->show();
        placeFloatingBar();
    }
}

void DocumentView::setInitialPage(int page) { initialPage_ = page; }

namespace {

const std::pair<PageMode, const char*> kModes[] = {
    {PageMode::SinglePage, "SinglePage"},
    {PageMode::Continuous, "Continuous"},
    {PageMode::Facing, "Facing"},
    {PageMode::FacingContinuous, "FacingContinuous"},
};

}  // namespace

void DocumentView::setInitialView(const LastView& view) {
    initialView_ = view;
    initialPage_ = view.page;
}

void DocumentView::setInitialAction(const links::LinkAction& action) {
    initialAction_ = action;
    initialPage_ = action.page;
}

LastView DocumentView::currentView() const {
    LastView v;
    v.page = view_->currentPage();
    v.zoomPercent = view_->zoomPercent();
    for (const auto& [mode, name] : kModes) {
        if (mode == view_->pageMode()) {
            v.layout = QLatin1String(name);
        }
    }
    return v;
}

bool DocumentView::save() {
    error_.clear();
    if (isViewOnly()) {
        error_ = QStringLiteral("not a PDF");
        return false;
    }
    try {
        const SaveReport report = session_->save().result();
        if (!report.ok()) {
            error_ = QString::fromStdString(report.message);
            return false;
        }
    } catch (const std::exception& e) {
        error_ = QString::fromUtf8(e.what());
        return false;
    }
    updateModified();
    return true;
}

void DocumentView::updateModified() {
    const bool modified = session_->isOpen() && session_->undoState().canUndo;
    if (modified != modified_) {
        modified_ = modified;
        emit modifiedChanged(modified);
    }
}

void DocumentView::placeFloatingBar() {
    if (floating_ == nullptr) {
        return;
    }
    floating_->adjustSize();
    const QSize s = floating_->sizeHint();
    const int margin = Theme::sizes().grid * 4;
    const QRect area = view_->geometry();
    floating_->setGeometry(area.left() + (area.width() - s.width()) / 2,
                           area.bottom() - margin - s.height() + 1, s.width(), s.height());
    floating_->raise();
}

// Панель «Найти» — в правом верхнем углу области страниц (левее полосы прокрутки), поверх вида.
void DocumentView::placeFindBar() {
    if (find_ == nullptr || !find_->isVisible()) {
        return;
    }
    const QSize s = find_->sizeHint();
    const int margin = Theme::sizes().grid * 4;
    const QRect area = view_->geometry();
    const int scroll = view_->verticalScrollBar()->isVisible() ? view_->verticalScrollBar()->width() : 0;
    const int width = std::min(s.width(), std::max(0, area.width() - scroll - margin * 2));
    find_->setGeometry(area.right() - scroll - margin - width + 1, area.top() + margin, width, s.height());
    find_->raise();
}

bool DocumentView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == find_ && (event->type() == QEvent::Show || event->type() == QEvent::LayoutRequest)) {
        QTimer::singleShot(0, this, &DocumentView::placeFindBar);
    }
    return QWidget::eventFilter(watched, event);
}

void DocumentView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    placeFloatingBar();
    placeFindBar();
}

void DocumentView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    placeFloatingBar();
    if (!defaultZoom_.isEmpty()) {
        // Масштаб по умолчанию — первым (размер вьюпорта известен после показа); начальный вид ниже его
        // меняет.
        QTimer::singleShot(0, this, [this, zoom = defaultZoom_] { applyDefaultZoom(zoom); });
        defaultZoom_.clear();
    }
    if (initialPage_ >= 0) {
        // После показа: размер вьюпорта известен — переход ставит страницу наверх.
        QTimer::singleShot(0, this, [this] {
            if (initialPage_ < 0) {
                return;
            }
            for (const auto& [mode, name] : kModes) {
                if (initialView_.layout == QLatin1String(name)) {
                    view_->setPageMode(mode);
                }
            }
            if (initialView_.zoomPercent > 0) {
                view_->setZoomPercent(static_cast<float>(initialView_.zoomPercent));
            }
            if (initialAction_) {
                links::LinkInfo link;
                link.page = initialAction_->page;
                link.action = *initialAction_;
                view_->activateLink(link);  // страница, вид (/Fit, /XYZ…), масштаб — как переход по ссылке
                initialAction_.reset();
            } else {
                view_->goToPage(initialPage_);
            }
            initialPage_ = -1;
        });
    }
}

}  // namespace ot::ui
