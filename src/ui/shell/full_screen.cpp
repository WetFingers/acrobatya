#include "ui/shell/full_screen.h"

#include <QCloseEvent>
#include <QFuture>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "services/document_session.h"
#include "services/nav_ops.h"
#include "services/settings.h"
#include "ui/view/page_view.h"

namespace ot::ui {

using presentation::PageTransition;
using presentation::TransitionType;

namespace {

constexpr int kMinTransitionMs = 100;
constexpr int kMaxTransitionMs = 5000;
constexpr int kBlinds = 6;

TransitionType fullScreenTypeFromName(const QString& name) {
    for (int t = 0; t <= static_cast<int>(TransitionType::Fade); ++t) {
        const auto type = static_cast<TransitionType>(t);
        if (name == QLatin1String(presentation::transitionName(type).data(),
                                  static_cast<qsizetype>(presentation::transitionName(type).size()))) {
            return type;
        }
    }
    return TransitionType::None;
}

}  // namespace

// ---------------------------------------------------------------- настройки

FullScreenSettings FullScreenSettings::load() {
    const Settings s;
    FullScreenSettings f;
    f.advanceSeconds = s.get(settings::kFullScreenAdvanceSeconds);
    f.useDocDuration = s.get(settings::kFullScreenUseDocDuration);
    f.loop = s.get(settings::kFullScreenLoop);
    f.escapeExits = s.get(settings::kFullScreenEscapeExits);
    f.clickAdvances = s.get(settings::kFullScreenClickAdvances);
    f.transitions = s.get(settings::kFullScreenTransitions);
    f.defaultTransition = s.get(settings::kFullScreenDefaultTransition);
    const QColor bg(s.get(settings::kFullScreenBackground));
    f.background = bg.isValid() ? bg : QColor(Qt::black);
    return f;
}

void FullScreenSettings::save() const {
    Settings s;
    s.set(settings::kFullScreenAdvanceSeconds, advanceSeconds);
    s.set(settings::kFullScreenUseDocDuration, useDocDuration);
    s.set(settings::kFullScreenLoop, loop);
    s.set(settings::kFullScreenEscapeExits, escapeExits);
    s.set(settings::kFullScreenClickAdvances, clickAdvances);
    s.set(settings::kFullScreenTransitions, transitions);
    s.set(settings::kFullScreenDefaultTransition, defaultTransition);
    s.set(settings::kFullScreenBackground, background.name());
}

// ---------------------------------------------------------------- переход поверх вида

// Прежний кадр поверх живого вида: по ходу перехода (0 → 1) его видимая часть убывает — под ним уже новая
// страница. Мышь проходит насквозь.
class TransitionOverlay : public QWidget {
public:
    explicit TransitionOverlay(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        anim_.setStartValue(0.0);
        anim_.setEndValue(1.0);
        QObject::connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            progress_ = v.toDouble();
            update();
        });
        QObject::connect(&anim_, &QVariantAnimation::finished, this, [this] { hide(); });
        hide();
    }

    void start(const QPixmap& from, const PageTransition& t, int ms) {
        from_ = from;
        t_ = t;
        progress_ = 0;
        anim_.stop();
        anim_.setDuration(ms);
        setGeometry(parentWidget()->rect());
        show();
        raise();
        anim_.start();
    }
    bool running() const { return anim_.state() == QAbstractAnimation::Running; }
    void finish() {
        anim_.stop();
        hide();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (from_.isNull()) {
            return;
        }
        QPainter p(this);
        const QRectF r = rect();
        const double k = progress_;
        const double w = r.width();
        const double h = r.height();
        switch (t_.type) {
            case TransitionType::Dissolve:
            case TransitionType::Fade:
            case TransitionType::Glitter:
            case TransitionType::Fly:
            case TransitionType::None:
                p.setOpacity(1.0 - k);
                p.drawPixmap(r.toRect(), from_);
                return;
            case TransitionType::Split: {
                // Прежний — по краям (наружу) или в середине (внутрь).
                QPainterPath clip;
                if (t_.vertical) {
                    const double band = (t_.outwards ? k : 1.0 - k) * w;
                    const QRectF middle((w - band) / 2, 0, band, h);
                    clip.addRect(r);
                    if (t_.outwards) {
                        QPainterPath hole;
                        hole.addRect(middle);
                        clip = clip.subtracted(hole);
                    } else {
                        clip = QPainterPath();
                        clip.addRect(middle);
                    }
                } else {
                    const double band = (t_.outwards ? k : 1.0 - k) * h;
                    const QRectF middle(0, (h - band) / 2, w, band);
                    clip.addRect(r);
                    if (t_.outwards) {
                        QPainterPath hole;
                        hole.addRect(middle);
                        clip = clip.subtracted(hole);
                    } else {
                        clip = QPainterPath();
                        clip.addRect(middle);
                    }
                }
                p.setClipPath(clip);
                break;
            }
            case TransitionType::Box: {
                const double s = t_.outwards ? k : 1.0 - k;
                const QRectF box((w - s * w) / 2, (h - s * h) / 2, s * w, s * h);
                QPainterPath clip;
                if (t_.outwards) {
                    clip.addRect(r);
                    QPainterPath hole;
                    hole.addRect(box);
                    clip = clip.subtracted(hole);
                } else {
                    clip.addRect(box);
                }
                p.setClipPath(clip);
                break;
            }
            case TransitionType::Blinds: {
                QPainterPath clip;
                for (int i = 0; i < kBlinds; ++i) {
                    if (t_.vertical) {
                        const double sw = w / kBlinds;
                        clip.addRect(QRectF(i * sw + k * sw, 0, (1.0 - k) * sw, h));
                    } else {
                        const double sh = h / kBlinds;
                        clip.addRect(QRectF(0, i * sh + k * sh, w, (1.0 - k) * sh));
                    }
                }
                p.setClipPath(clip);
                break;
            }
            case TransitionType::Wipe:
            case TransitionType::Push:
            case TransitionType::Cover:
            case TransitionType::Uncover: {
                // /Di: 0 — слева направо, 90 — снизу вверх, 180 — справа налево, 270 — сверху вниз.
                const int d = ((t_.direction % 360) + 360) % 360;
                QRectF keep;
                if (d == 90) {
                    keep = QRectF(0, 0, w, (1.0 - k) * h);
                } else if (d == 180) {
                    keep = QRectF(0, 0, (1.0 - k) * w, h);
                } else if (d == 270) {
                    keep = QRectF(0, k * h, w, (1.0 - k) * h);
                } else {
                    keep = QRectF(k * w, 0, (1.0 - k) * w, h);
                }
                p.setClipRect(keep);
                break;
            }
        }
        p.drawPixmap(r.toRect(), from_);
    }

private:
    QVariantAnimation anim_;
    QPixmap from_;
    PageTransition t_;
    double progress_ = 0;
};

// ---------------------------------------------------------------- окно

FullScreenWindow::FullScreenWindow(std::shared_ptr<DocumentSession> session, RenderPool* pool,
                                   TileCache* cache, int page, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint), session_(std::move(session)), startPage_(page) {
    setObjectName(QStringLiteral("fullScreenWindow"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Полноэкранный режим"));
    settings_ = FullScreenSettings::load();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    view_ = new PageView(this);
    view_->setObjectName(QStringLiteral("fullScreenView"));
    view_->setFrameShape(QFrame::NoFrame);
    view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view_->setRespectDocumentLayout(false);
    view_->setDocument(session_.get(), pool, cache);
    view_->setPageMode(PageMode::SinglePage);
    view_->setPageBorder(false);
    view_->setCanvasColor(settings_.background);
    column->addWidget(view_);
    overlay_ = new TransitionOverlay(this);
    view_->installEventFilter(this);
    view_->viewport()->installEventFilter(this);
    view_->viewport()->setCursor(Qt::ArrowCursor);
    advance_.setSingleShot(true);
    connect(&advance_, &QTimer::timeout, this, [this] {
        const int last = view_->pageCount() - 1;
        if (currentPage() < last) {
            next();
        } else if (settings_.loop && last > 0) {
            showPage(0, true);
        }
    });
    nav::transitions(*session_).then(this, [this](const std::vector<PageTransition>& list) {
        transitions_ = list;
        scheduleAdvance();
        emit transitionsLoaded();
    });
}

FullScreenWindow::~FullScreenWindow() { view_->setDocument(nullptr); }

void FullScreenWindow::start(QScreen* screen) {
    if (screen != nullptr) {
        setScreen(screen);
        setGeometry(screen->geometry());
    }
    showFullScreen();
    activateWindow();
    view_->setFocus(Qt::OtherFocusReason);
}

void FullScreenWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!started_) {
        started_ = true;
        // После показа: размер известен — страница целиком.
        QTimer::singleShot(0, this, [this] {
            view_->fit(FitMode::Page);
            view_->goToPage(startPage_);
            scheduleAdvance();
        });
    }
}

void FullScreenWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    overlay_->setGeometry(rect());
}

int FullScreenWindow::currentPage() const { return view_->currentPage(); }

void FullScreenWindow::setSettings(const FullScreenSettings& settings) {
    settings_ = settings;
    view_->setCanvasColor(settings_.background);
    scheduleAdvance();
}

bool FullScreenWindow::isTransitionRunning() const { return overlay_->running(); }

void FullScreenWindow::next() {
    if (currentPage() + 1 < view_->pageCount()) {
        showPage(currentPage() + 1, true);
    } else if (settings_.loop && view_->pageCount() > 1) {
        showPage(0, true);
    }
}

void FullScreenWindow::previous() {
    if (currentPage() > 0) {
        showPage(currentPage() - 1, true);
    }
}

void FullScreenWindow::goTo(int page) { showPage(page, true); }

PageTransition FullScreenWindow::transitionFor(int page) const {
    PageTransition t;
    if (page >= 0 && page < static_cast<int>(transitions_.size())) {
        t = transitions_[static_cast<std::size_t>(page)];
    }
    if (!settings_.transitions) {
        t.type = TransitionType::None;
    }
    if (t.type == TransitionType::None && !settings_.defaultTransition.isEmpty()) {
        t.type = fullScreenTypeFromName(settings_.defaultTransition);
        t.duration = 1.0f;
    }
    return t;
}

// Переход описан у страницы, на которую переходят (ISO 32000 12.4.4.1).
void FullScreenWindow::showPage(int page, bool animate) {
    if (page < 0 || page >= view_->pageCount() || page == currentPage()) {
        return;
    }
    const PageTransition t = transitionFor(page);
    lastTransition_ =
        QString::fromLatin1(presentation::transitionName(t.type).data(),
                            static_cast<qsizetype>(presentation::transitionName(t.type).size()));
    if (animate && t.type != TransitionType::None && isVisible()) {
        const int ms = std::clamp(static_cast<int>(std::lround(t.duration * 1000.0f)), kMinTransitionMs,
                                  kMaxTransitionMs);
        overlay_->start(view_->grab(), t, ms);
    } else {
        overlay_->finish();
    }
    view_->goToPage(page);
    scheduleAdvance();
}

void FullScreenWindow::scheduleAdvance() {
    advance_.stop();
    double seconds = settings_.advanceSeconds;
    if (seconds <= 0 && settings_.useDocDuration) {
        const int page = currentPage();
        if (page >= 0 && page < static_cast<int>(transitions_.size())) {
            seconds = transitions_[static_cast<std::size_t>(page)].pageDuration;
        }
    }
    if (seconds > 0 && (currentPage() + 1 < view_->pageCount() || settings_.loop)) {
        advance_.start(static_cast<int>(seconds * 1000.0));
    }
}

bool FullScreenWindow::handleKey(QKeyEvent* event) {
    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers();
    if (key == Qt::Key_L && (mods & Qt::ControlModifier) != 0) {
        close();
        return true;
    }
    if (key == Qt::Key_Escape) {
        if (settings_.escapeExits) {
            close();
        }
        return true;
    }
    switch (key) {
        case Qt::Key_Right:
        case Qt::Key_Down:
        case Qt::Key_PageDown:
        case Qt::Key_Space:
        case Qt::Key_Return:
        case Qt::Key_Enter:
            next();
            return true;
        case Qt::Key_Left:
        case Qt::Key_Up:
        case Qt::Key_PageUp:
        case Qt::Key_Backspace:
            previous();
            return true;
        case Qt::Key_Home:
            showPage(0, true);
            return true;
        case Qt::Key_End:
            showPage(view_->pageCount() - 1, true);
            return true;
        default:
            return false;
    }
}

bool FullScreenWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == view_ && event->type() == QEvent::KeyPress) {
        if (handleKey(static_cast<QKeyEvent*>(event))) {
            return true;
        }
    }
    if (watched == view_->viewport()) {
        switch (event->type()) {
            case QEvent::MouseButtonPress: {
                auto* e = static_cast<QMouseEvent*>(event);
                if (settings_.clickAdvances) {
                    if (e->button() == Qt::LeftButton) {
                        next();
                    } else if (e->button() == Qt::RightButton) {
                        previous();
                    }
                }
                return true;
            }
            case QEvent::MouseButtonRelease:
            case QEvent::MouseButtonDblClick:
            case QEvent::MouseMove:
            case QEvent::ContextMenu:
                return true;  // ни выделения, ни инструментов
            case QEvent::Wheel: {
                auto* e = static_cast<QWheelEvent*>(event);
                if (e->angleDelta().y() < 0) {
                    next();
                } else if (e->angleDelta().y() > 0) {
                    previous();
                }
                return true;
            }
            default:
                break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void FullScreenWindow::keyPressEvent(QKeyEvent* event) {
    if (!handleKey(event)) {
        QWidget::keyPressEvent(event);
    }
}

void FullScreenWindow::closeEvent(QCloseEvent* event) {
    advance_.stop();
    overlay_->finish();
    emit closed(currentPage());
    QWidget::closeEvent(event);
}

}  // namespace ot::ui
