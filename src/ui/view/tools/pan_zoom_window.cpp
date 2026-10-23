// «Панорамирование и масштаб» (эталон 3.1 «Масштаб»; view-05): панель с миниатюрой текущей страницы и рамкой
// видимой области — щелчок и перетаскивание по миниатюре двигают вид, ◀ ▶ — страницы, − + — масштаб. На самой
// странице инструмент ведёт себя как «Рука». Миниатюра — задача документа (ClipboardIo::snapshot).
#include <QCoreApplication>
#include <QFuture>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>

#include <algorithm>
#include <cmath>

#include "services/clipboard_io.h"
#include "services/document_session.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/view/tools/tool_panels.h"
#include "ui/view/tools/view_tool.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

// ---------------------------------------------------------------- PanZoomPanel

PanZoomPanel::PanZoomPanel(QWidget* host) : ToolPanel(tr("Панорамирование и масштаб"), host) {
    setObjectName(QStringLiteral("panZoomPanel"));
    const Metrics& m = Theme::sizes();
    thumb_ = new ToolImage(this);
    thumb_->setObjectName(QStringLiteral("panZoomThumb"));
    thumb_->setFixedSize(m.toolWindowWidth - 4 * m.grid, m.toolWindowHeight);
    thumb_->setInteractive(true);
    thumb_->setCursor(Qt::PointingHandCursor);
    connect(thumb_, &ToolImage::pointed, this, &PanZoomPanel::pointed);
    addBody(thumb_);

    auto* footer = new QWidget(this);
    auto* row = new QHBoxLayout(footer);
    row->setContentsMargins(0, 0, 0, 0);
    auto button = [&](const char* icon, const QString& tip, const char* name) {
        auto* b = new ToolButton(QString::fromLatin1(icon), {}, footer);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setObjectName(QString::fromLatin1(name));
        row->addWidget(b);
        return b;
    };
    ToolButton* prev = button("chevron-left", tr("Предыдущая страница"), "panZoomPrevious");
    ToolButton* next = button("chevron-right", tr("Следующая страница"), "panZoomNext");
    row->addStretch(1);
    ToolButton* out = button("minus", tr("Уменьшить"), "panZoomOut");
    zoom_ = new QLabel(footer);
    zoom_->setObjectName(QStringLiteral("panZoomText"));
    zoom_->setFont(numberFont(m));
    zoom_->setAlignment(Qt::AlignCenter);
    row->addWidget(zoom_);
    ToolButton* in = button("plus", tr("Увеличить"), "panZoomIn");
    addFooter(footer);
    connect(prev, &ToolButton::clicked, this, &PanZoomPanel::previousPage);
    connect(next, &ToolButton::clicked, this, &PanZoomPanel::nextPage);
    connect(out, &ToolButton::clicked, this, &PanZoomPanel::zoomOut);
    connect(in, &ToolButton::clicked, this, &PanZoomPanel::zoomIn);
}

QSize PanZoomPanel::thumbnailSize() const { return thumb_->size(); }

void PanZoomPanel::setZoomText(const QString& text) { zoom_->setText(text); }

// ---------------------------------------------------------------- PanZoomTool

namespace {

// Видимая часть страницы в долях её рамки на экране (поворот вида уже в ней — как и в миниатюре).
QRectF panZoomVisibleFraction(const PageView& view, int page) {
    const QRectF r = view.pageViewportRect(page);
    if (r.isEmpty()) {
        return {};
    }
    const QRectF shown = r.intersected(QRectF(view.viewport()->rect()));
    if (shown.isEmpty()) {
        return {};
    }
    return {(shown.left() - r.left()) / r.width(), (shown.top() - r.top()) / r.height(),
            shown.width() / r.width(), shown.height() / r.height()};
}

}  // namespace

PanZoomTool::PanZoomTool(PageView& view, ToolController& controller)
    : ViewTool(view, controller), hand_(view, controller) {}

PanZoomTool::~PanZoomTool() { delete panel_.data(); }

QString PanZoomTool::title() const {
    return QCoreApplication::translate("ot::ui::PanZoomTool", "Панорамирование и масштаб");
}

void PanZoomTool::activate() {
    if (panel_ == nullptr) {
        panel_ = new PanZoomPanel(&view_);
        PanZoomPanel* panel = panel_;
        QObject::connect(panel, &ToolPanel::closeRequested, &controller_,
                         [this] { controller_.setTool(ToolController::defaultTool()); });
        QObject::connect(panel, &PanZoomPanel::previousPage, &view_, &PageView::previousPage);
        QObject::connect(panel, &PanZoomPanel::nextPage, &view_, &PageView::nextPage);
        QObject::connect(panel, &PanZoomPanel::zoomIn, &view_, [this] { view_.zoomIn(); });
        QObject::connect(panel, &PanZoomPanel::zoomOut, &view_, [this] { view_.zoomOut(); });
        // Точка миниатюры — в центр окна.
        QObject::connect(panel, &PanZoomPanel::pointed, &view_, [this](QPointF f) {
            const int page = view_.currentPage();
            const QRectF r = view_.pageViewportRect(page);
            if (r.isEmpty()) {
                return;
            }
            const QPointF target(r.left() + f.x() * r.width(), r.top() + f.y() * r.height());
            const QPointF shift =
                target - QPointF(view_.viewport()->width() / 2.0, view_.viewport()->height() / 2.0);
            view_.horizontalScrollBar()->setValue(view_.horizontalScrollBar()->value() + qRound(shift.x()));
            view_.verticalScrollBar()->setValue(view_.verticalScrollBar()->value() + qRound(shift.y()));
        });
        // Рамка следует за видом; миниатюра — за текущей страницей, масштабом раскладки и правками.
        auto frame = [this] {
            if (panel_ != nullptr && panel_->isVisible()) {
                panel_->setFrame(panZoomVisibleFraction(view_, view_.currentPage()));
                panel_->setZoomText(QCoreApplication::translate("ot::ui::PanZoomTool", "%1 %")
                                        .arg(qRound(view_.zoomPercent())));
            }
        };
        auto thumbnail = [this, frame] {
            if (panel_ == nullptr || !panel_->isVisible()) {
                return;
            }
            frame();
            DocumentSession* session = view_.document();
            const int page = view_.currentPage();
            if (session == nullptr || !session->isOpen() || page < 0 || page >= view_.pageCount()) {
                panel_->setThumbnail({});
                return;
            }
            // Страница целиком, вписанная в миниатюру (с поворотом вида).
            const QSizeF pt = view_.pageSizePoints(page);
            const int rotation = view_.viewRotation();
            const QSizeF shown = (rotation == 90 || rotation == 270) ? pt.transposed() : pt;
            const QSize box = panel_->thumbnailSize();
            const qreal dpr = view_.devicePixelRatioF();
            const qreal dpi =
                std::min(box.width() / shown.width(), box.height() / shown.height()) * 72.0 * dpr;
            QFuture<QImage> future =
                ClipboardIo::snapshot(*session, page, QRectF(QPointF(0, 0), pt), std::max<qreal>(dpi, 1.0));
            future.then(panel_, [this, rotation, dpr, page](QFuture<QImage> done) {
                QImage image;
                try {
                    image = done.result();
                } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): без миниатюры — пусто
                }
                if (page != view_.currentPage() || panel_ == nullptr) {
                    return;  // страница уже другая — придёт своя
                }
                if (!image.isNull() && rotation != 0) {
                    image = image.transformed(QTransform().rotate(rotation));
                }
                image.setDevicePixelRatio(dpr);
                panel_->setThumbnail(image);
            });
        };
        QObject::connect(view_.horizontalScrollBar(), &QScrollBar::valueChanged, panel, frame);
        QObject::connect(view_.verticalScrollBar(), &QScrollBar::valueChanged, panel, frame);
        QObject::connect(&view_, &PageView::zoomChanged, panel, frame);
        QObject::connect(&view_, &PageView::layoutChanged, panel, frame);
        QObject::connect(&view_, &PageView::currentPageChanged, panel, thumbnail);
        refresh_ = thumbnail;
    }
    panel_->show();
    panel_->place();
    refresh_();
}

void PanZoomTool::deactivate() {
    hand_.cancel();
    if (panel_ != nullptr) {
        panel_->hide();
    }
}

}  // namespace ot::ui
