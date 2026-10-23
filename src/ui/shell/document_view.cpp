#include "ui/shell/document_view.h"

#include <QFileInfo>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <exception>

#include "ui/theme/theme.h"
#include "ui/view/page_view.h"
#include "ui/widgets/message_bar.h"

namespace ot::ui {

DocumentView::DocumentView(std::shared_ptr<DocumentSession> session, RenderPool* pool, TileCache* cache,
                           QWidget* parent)
    : QWidget(parent), session_(std::move(session)) {
    setObjectName(QStringLiteral("documentView"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    messages_ = new QWidget(this);
    messages_->setObjectName(QStringLiteral("messageStack"));
    messageLayout_ = new QVBoxLayout(messages_);
    const Metrics& m = Theme::sizes();
    messageLayout_->setContentsMargins(m.grid * 2, m.grid * 2, m.grid * 2, 0);
    messageLayout_->setSpacing(m.grid);
    messages_->hide();
    column->addWidget(messages_);
    view_ = new PageView(this);
    view_->setObjectName(QStringLiteral("pageView"));
    column->addWidget(view_, 1);
    view_->setDocument(session_.get(), pool, cache);
    setFocusProxy(view_);

    connect(session_.get(), &DocumentSession::undoStateChanged, this, &DocumentView::updateModified,
            Qt::QueuedConnection);
    connect(session_.get(), &DocumentSession::saved, this, &DocumentView::updateModified,
            Qt::QueuedConnection);
    updateModified();

    if (isViewOnly()) {
        MessageBar* bar = addMessage(
            Tone::Info,
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
DocumentView::~DocumentView() { view_->setDocument(nullptr); }

QString DocumentView::path() const { return session_->path(); }

QString DocumentView::title() const { return QFileInfo(session_->path()).fileName(); }

bool DocumentView::isViewOnly() const { return !session_->summary().isPdf; }

MessageBar* DocumentView::addMessage(Tone tone, const QString& text) {
    auto* bar = new MessageBar(tone, text, messages_);
    bar->setClosable(true);
    connect(bar, &MessageBar::closed, this, [this, bar] {
        bar->setProperty("otClosed", true);
        bar->hide();
        bar->deleteLater();
        QTimer::singleShot(0, this, [this] { messages_->setVisible(!messages().isEmpty()); });
    });
    messageLayout_->addWidget(bar);
    messages_->show();
    return bar;
}

QList<MessageBar*> DocumentView::messages() const {
    QList<MessageBar*> out;
    for (MessageBar* bar : messages_->findChildren<MessageBar*>(Qt::FindDirectChildrenOnly)) {
        if (!bar->property("otClosed").toBool()) {
            out << bar;
        }
    }
    return out;
}

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

void DocumentView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    placeFloatingBar();
}

void DocumentView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    placeFloatingBar();
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
            view_->goToPage(initialPage_);
            initialPage_ = -1;
        });
    }
}

}  // namespace ot::ui
