// «Лупа» (эталон 3.1 «Масштаб»; view-05): щелчок на странице ставит область, рамку можно тянуть; панель
// показывает её при 200–800 % (ползунок). Растр области — задача документа (ClipboardIo::snapshot) при
// масштабе лупы и dpr экрана; поворот вида — поворотом растра. Здесь же общее для панелей инструментов.
#include <QCoreApplication>
#include <QEvent>
#include <QFuture>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "core/render/fit.h"
#include "services/clipboard_io.h"
#include "services/document_session.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/view/tools/tool_panels.h"
#include "ui/view/tools/view_tool.h"
#include "ui/widgets/panel_header.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

namespace {

constexpr int kLoupeStep = 25;  // шаг ползунка, %

}  // namespace

// ---------------------------------------------------------------- ToolImage

ToolImage::ToolImage(QWidget* parent) : QWidget(parent) { setAttribute(Qt::WA_OpaquePaintEvent); }

void ToolImage::setImage(const QImage& image) {
    image_ = image;
    update();
}

void ToolImage::setFrame(const QRectF& fraction) {
    frame_ = fraction;
    update();
}

QRectF ToolImage::imageRect() const {
    if (image_.isNull()) {
        return {};
    }
    const QSizeF logical = image_.deviceIndependentSize();
    const double k = std::min({1.0, width() / logical.width(), height() / logical.height()});
    const QSizeF s = logical * k;
    return {(width() - s.width()) / 2.0, (height() - s.height()) / 2.0, s.width(), s.height()};
}

void ToolImage::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    const Palette& c = Theme::colors();
    p.fillRect(rect(), c.canvas.color());
    if (image_.isNull()) {
        return;
    }
    const QRectF r = imageRect();
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(r, image_);
    if (!frame_.isEmpty()) {
        const QRectF f(r.left() + frame_.left() * r.width(), r.top() + frame_.top() * r.height(),
                       frame_.width() * r.width(), frame_.height() * r.height());
        QPen pen(c.accent.color(), Theme::sizes().focusRing);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(f.intersected(r));
    }
}

void ToolImage::mousePressEvent(QMouseEvent* event) {
    if (!interactive_ || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QRectF r = imageRect();
    if (!r.isEmpty()) {
        emit pointed(QPointF((event->position().x() - r.left()) / r.width(),
                             (event->position().y() - r.top()) / r.height()));
    }
}

void ToolImage::mouseMoveEvent(QMouseEvent* event) {
    if (!interactive_ || (event->buttons() & Qt::LeftButton) == 0) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const QRectF r = imageRect();
    if (!r.isEmpty()) {
        emit pointed(QPointF(std::clamp((event->position().x() - r.left()) / r.width(), 0.0, 1.0),
                             std::clamp((event->position().y() - r.top()) / r.height(), 0.0, 1.0)));
    }
}

void ToolImage::mouseReleaseEvent(QMouseEvent* event) {
    if (interactive_ && event->button() == Qt::LeftButton) {
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

// ---------------------------------------------------------------- ToolWindow

ToolWindow::ToolWindow(const QString& title, QWidget* host) : QFrame(host) {
    setAttribute(Qt::WA_StyledBackground, false);
    auto* layout = new QVBoxLayout(this);
    header_ = new PanelHeader(title, this);
    ToolButton* close = header_->addAction(QStringLiteral("x"), tr("Закрыть"));
    close->setObjectName(QStringLiteral("toolPanelClose"));
    connect(close, &ToolButton::clicked, this, &ToolWindow::closeRequested);
    layout->addWidget(header_);
    host->installEventFilter(this);
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, [this] {
        applyMetrics();
        place();
        update();
    });
    hide();
}

void ToolWindow::applyMetrics() {
    const Metrics& m = Theme::sizes();
    layout()->setContentsMargins(m.grid * 2, m.grid, m.grid * 2, m.grid * 2);
    layout()->setSpacing(m.grid * 2);
    setFixedWidth(m.toolWindowWidth);
}

void ToolWindow::addBody(QWidget* body) { static_cast<QVBoxLayout*>(layout())->addWidget(body); }

void ToolWindow::addFooter(QWidget* footer) { static_cast<QVBoxLayout*>(layout())->addWidget(footer); }

void ToolWindow::place() {
    auto* view = qobject_cast<QAbstractScrollArea*>(parentWidget());
    if (view == nullptr) {
        return;
    }
    adjustSize();
    const QRect viewport = view->viewport()->geometry();
    const int margin = Theme::sizes().grid * 2;
    move(viewport.right() - width() - margin + 1, viewport.top() + margin);
    raise();
}

bool ToolWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible()) {
        place();
    }
    return QFrame::eventFilter(watched, event);
}

void ToolWindow::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const Palette& c = Theme::colors();
    const Metrics& m = Theme::sizes();
    p.setPen(QPen(c.border.color(), m.pageBorder));
    p.setBrush(c.surface.color());
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), m.radiusLarge, m.radiusLarge);
}

// ---------------------------------------------------------------- LoupePanel

LoupePanel::LoupePanel(QWidget* host) : ToolWindow(tr("Лупа"), host) {
    setObjectName(QStringLiteral("loupePanel"));
    const Metrics& m = Theme::sizes();
    image_ = new ToolImage(this);
    image_->setObjectName(QStringLiteral("loupeImage"));
    image_->setFixedSize(m.toolWindowWidth - 4 * m.grid, m.toolWindowHeight);
    addBody(image_);
    auto* footer = new QWidget(this);
    auto* row = new QHBoxLayout(footer);
    row->setContentsMargins(0, 0, 0, 0);
    slider_ = new QSlider(Qt::Horizontal, footer);
    slider_->setObjectName(QStringLiteral("loupeZoom"));
    slider_->setRange(LoupeTool::kMinPercent, LoupeTool::kMaxPercent);
    slider_->setSingleStep(kLoupeStep);
    slider_->setPageStep(4 * kLoupeStep);
    slider_->setAccessibleName(tr("Масштаб лупы"));
    label_ = new QLabel(footer);
    label_->setObjectName(QStringLiteral("loupeZoomText"));
    label_->setFont(numberFont(m));
    row->addWidget(slider_, 1);
    row->addWidget(label_);
    addFooter(footer);
    connect(slider_, &QSlider::valueChanged, this, [this](int value) {
        const int snapped = qRound(value / static_cast<double>(kLoupeStep)) * kLoupeStep;
        label_->setText(tr("%1 %").arg(snapped));
        emit percentChanged(snapped);
    });
    setPercent(LoupeTool::kDefaultPercent);
}

QSize LoupePanel::imageSize() const { return image_->size(); }

void LoupePanel::setPercent(int percent) {
    const QSignalBlocker block(slider_);
    slider_->setValue(percent);
    label_->setText(tr("%1 %").arg(percent));
}

int LoupePanel::percent() const { return slider_->value(); }

// ---------------------------------------------------------------- LoupeTool

LoupeTool::LoupeTool(PageView& view, ToolController& controller) : ViewTool(view, controller) {}

LoupeTool::~LoupeTool() { delete panel_.data(); }

QString LoupeTool::title() const { return QCoreApplication::translate("ot::ui::LoupeTool", "Лупа"); }

void LoupeTool::activate() {
    if (panel_ == nullptr) {
        panel_ = new LoupePanel(&view_);
        QObject::connect(panel_, &LoupePanel::percentChanged, &controller_, [this](int p) { setPercent(p); });
        QObject::connect(panel_, &ToolWindow::closeRequested, &controller_,
                         [this] { controller_.setTool(ToolController::defaultTool()); });
        // Вид листают и масштабируют — область на экране сдвигается вместе со страницей.
        QObject::connect(&view_, &PageView::layoutChanged, panel_, [this] { view_.viewport()->update(); });
    }
    panel_->setPercent(percent_);
    panel_->show();
    panel_->place();
    if (page_ < 0 && view_.pageCount() > 0) {
        // Первая область — центр окна.
        const QPointF c(view_.viewport()->width() / 2.0, view_.viewport()->height() / 2.0);
        const int page = view_.pageAtViewport(c);
        if (page >= 0) {
            setCenter(page, view_.viewportToPage(page, c));
        }
    } else {
        refresh();
    }
}

void LoupeTool::deactivate() {
    dragging_ = false;
    if (panel_ != nullptr) {
        panel_->hide();
    }
    view_.viewport()->update();
}

void LoupeTool::setPercent(int percent) {
    percent = std::clamp(percent, kMinPercent, kMaxPercent);
    if (percent == percent_) {
        return;
    }
    percent_ = percent;
    if (panel_ != nullptr) {
        panel_->setPercent(percent);
    }
    refresh();
}

QRectF LoupeTool::area() const {
    if (page_ < 0) {
        return {};
    }
    // Размер картинки лупы в пунктах при её масштабе; поворот вида 90/270 меняет стороны.
    const QSize image = panel_ != nullptr
                            ? panel_->imageSize()
                            : QSize(Theme::sizes().toolWindowWidth, Theme::sizes().toolWindowHeight);
    const double zoom = percentToZoom(static_cast<float>(percent_), view_.screenDpi());
    QSizeF size(image.width() / zoom, image.height() / zoom);
    if (view_.viewRotation() == 90 || view_.viewRotation() == 270) {
        size.transpose();
    }
    return {center_.x() - size.width() / 2.0, center_.y() - size.height() / 2.0, size.width(), size.height()};
}

void LoupeTool::setCenter(int page, QPointF pagePoint) {
    if (page < 0 || page >= view_.pageCount()) {
        return;
    }
    page_ = page;
    const QSizeF pt = view_.pageSizePoints(page);
    center_ =
        QPointF(std::clamp(pagePoint.x(), 0.0, pt.width()), std::clamp(pagePoint.y(), 0.0, pt.height()));
    view_.viewport()->update();
    refresh();
}

void LoupeTool::documentChanged() {
    page_ = -1;
    ++request_;
    dragging_ = false;
    if (panel_ != nullptr) {
        panel_->setImage({});
        if (panel_->isVisible() && view_.pageCount() > 0) {
            activate();  // новая область — центр окна
        }
    }
}

void LoupeTool::pageChanged(int page) {
    if (page == page_) {
        refresh();
    }
}

void LoupeTool::refresh() {
    DocumentSession* session = view_.document();
    if (panel_ == nullptr || page_ < 0 || session == nullptr || !session->isOpen()) {
        return;
    }
    const quint64 request = ++request_;
    const qreal dpr = view_.devicePixelRatioF();
    const qreal dpi = percentToZoom(static_cast<float>(percent_), view_.screenDpi()) * 72.0 * dpr;
    const int rotation = view_.viewRotation();
    QFuture<QImage> future = ClipboardIo::snapshot(*session, page_, area(), dpi);
    future.then(&controller_, [this, request, rotation, dpr](QFuture<QImage> done) {
        QImage image;
        try {
            image = done.result();
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет растра — пустая лупа
        }
        if (!image.isNull() && rotation != 0) {
            image = image.transformed(QTransform().rotate(rotation));
        }
        image.setDevicePixelRatio(dpr);
        imageArrived(image, request);
    });
}

void LoupeTool::imageArrived(const QImage& image, quint64 request) {
    if (request != request_ || panel_ == nullptr) {
        return;  // устарел: область или масштаб уже другие
    }
    panel_->setImage(image);
}

bool LoupeTool::mousePress(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return false;
    }
    const int page = view_.pageAtViewport(event->position());
    if (page < 0) {
        return true;
    }
    const QPointF pt = view_.viewportToPage(page, event->position());
    if (page == page_ && area().contains(pt)) {
        dragging_ = true;  // тянуть рамку за точку внутри
        dragOffset_ = center_ - pt;
        return true;
    }
    setCenter(page, pt);
    dragging_ = true;
    dragOffset_ = QPointF();
    return true;
}

bool LoupeTool::mouseMove(QMouseEvent* event) {
    if (!dragging_ || page_ < 0) {
        return false;
    }
    setCenter(page_, view_.viewportToPage(page_, event->position()) + dragOffset_);
    return true;
}

bool LoupeTool::mouseRelease(QMouseEvent* event) {
    if (!dragging_ || event->button() != Qt::LeftButton) {
        return false;
    }
    dragging_ = false;
    return true;
}

void LoupeTool::paintOverlay(QPainter& painter) {
    if (page_ < 0 || view_.pageViewportRect(page_).isNull()) {
        return;
    }
    const QRectF r = view_.pageTransform(page_).mapRect(area());
    QPen pen(Theme::colors().accent.color(), Theme::sizes().focusRing);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(r);
}

QCursor LoupeTool::cursor(QPointF point, Qt::KeyboardModifiers /*modifiers*/) const {
    if (dragging_) {
        return QCursor(Qt::ClosedHandCursor);
    }
    const int page = view_.pageAtViewport(point);
    if (page >= 0 && page == page_ && area().contains(view_.viewportToPage(page, point))) {
        return QCursor(Qt::OpenHandCursor);
    }
    const int size = Theme::sizes().iconPanel;
    return QCursor(Icons::get(QStringLiteral("search"), IconTone::Text, size, view_.devicePixelRatioF()),
                   size * 11 / 24, size * 11 / 24);
}

}  // namespace ot::ui
