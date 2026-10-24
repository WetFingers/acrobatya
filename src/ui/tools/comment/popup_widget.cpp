#include "ui/tools/comment/popup_widget.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QFuture>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>

#include "services/annot_ops.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/theme/tokens.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

namespace {

QColor popupColor(const annot::AnnotColor& c) {
    if (c.empty()) {
        return Theme::colors().surface.color();
    }
    return QColor::fromRgb(static_cast<QRgb>(c.toRgb()));
}

// Текст на цвете комментария (данные документа): чёрный или белый из палитры цветов комментариев.
QColor popupInk(const QColor& background) {
    const Rgba& black = kAnnotationColors[10];
    const Rgba& white = kAnnotationColors[11];
    const double luminance =
        0.2126 * background.redF() + 0.7152 * background.greenF() + 0.0722 * background.blueF();
    return luminance > 0.5 ? black.color() : white.color();
}

QRectF popupQRect(const RectF& r) { return QRectF(QPointF(r.x0, r.y0), QPointF(r.x1, r.y1)); }

}  // namespace

// ---------------------------------------------------------------- PopupWidget

PopupWidget::PopupWidget(PageView& view, const annot::AnnotRef& ref)
    : QFrame(view.viewport()), view_(view), ref_(ref) {
    setObjectName(QStringLiteral("notePopup"));
    setFrameShape(QFrame::Box);
    setAutoFillBackground(true);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    header_ = new QWidget(this);
    header_->setObjectName(QStringLiteral("notePopupHeader"));
    header_->setAutoFillBackground(true);
    auto* row = new QHBoxLayout(header_);
    const Metrics& m = Theme::sizes();
    row->setContentsMargins(m.grid * 2, m.grid, m.grid, m.grid);
    row->setSpacing(m.grid);
    author_ = new QLabel(header_);
    author_->setObjectName(QStringLiteral("notePopupAuthor"));
    QFont bold = author_->font();
    bold.setBold(true);
    author_->setFont(bold);
    date_ = new QLabel(header_);
    date_->setObjectName(QStringLiteral("notePopupDate"));
    close_ = new QToolButton(header_);
    close_->setObjectName(QStringLiteral("notePopupClose"));
    close_->setAutoRaise(true);
    close_->setIcon(Icons::icon(QStringLiteral("x")));
    close_->setToolTip(tr("Закрыть окно записки"));
    close_->setAccessibleName(tr("Закрыть окно записки"));
    row->addWidget(author_, 1);
    row->addWidget(date_);
    row->addWidget(close_);
    column->addWidget(header_);

    editor_ = new QPlainTextEdit(this);
    editor_->setObjectName(QStringLiteral("notePopupText"));
    editor_->setFrameShape(QFrame::NoFrame);
    editor_->setAccessibleName(tr("Текст записки"));
    editor_->setPlaceholderText(tr("Добавьте комментарий"));
    const Settings settings;
    editor_->setFont(
        QFont(settings.get(settings::kCommentPopupFont), settings.get(settings::kCommentPopupFontSize)));
    column->addWidget(editor_, 1);

    timer_.setSingleShot(true);
    timer_.setInterval(kCommitDelayMs);
    connect(&timer_, &QTimer::timeout, this, &PopupWidget::flush);
    connect(editor_, &QPlainTextEdit::textChanged, this, [this] {
        if (!loading_) {
            timer_.start();
        }
    });
    connect(close_, &QToolButton::clicked, this, [this] { emit closeRequested(ref_); });
    editor_->setReadOnly(!view_.tools().editable());
    resize(m.notePopupWidth, m.notePopupHeight);
    applyStyle();
}

PopupWidget::~PopupWidget() = default;

void PopupWidget::applyStyle() {
    // Заголовок — цветом комментария (данные документа, как у Acrobat); таблица стилей темы перекрыла бы
    // палитру, поэтому — свой стиль виджета.
    const QColor bg = popupColor(props_.color);
    header_->setStyleSheet(
        QStringLiteral("#notePopupHeader { background-color: %1; } #notePopupHeader QLabel "
                       "{ color: %2; background: transparent; }")
            .arg(bg.name(QColor::HexRgb), popupInk(bg).name(QColor::HexRgb)));
}

void PopupWidget::setProps(const annot::AnnotProps& props) {
    props_ = props;
    ref_.nm = props.nm;
    author_->setText(props.author.empty() ? tr("Автор не указан") : QString::fromStdString(props.author));
    const std::int64_t when = props.modified > 0 ? props.modified : props.created;
    date_->setText(when > 0 ? QLocale().toString(QDateTime::fromSecsSinceEpoch(when), QLocale::ShortFormat)
                            : QString());
    const QString text = QString::fromStdString(props.contents);
    // Набор не перебивается своим же ответом: идущая правка важнее перечитанного текста.
    if (!timer_.isActive() && editor_->toPlainText() != text) {
        loading_ = true;
        editor_->setPlainText(text);
        loading_ = false;
    }
    committed_ = text;
    editor_->setReadOnly(!view_.tools().editable());
    applyStyle();
    reposition();
}

void PopupWidget::flush() {
    timer_.stop();
    const QString text = editor_->toPlainText();
    DocumentSession* session = view_.document();
    if (text == committed_ || session == nullptr || !view_.tools().editable()) {
        return;
    }
    committed_ = text;
    props_.contents = text.toStdString();
    annot::AnnotPatch patch;
    patch.contents = text.toStdString();
    annot::update(*session, ref_, std::move(patch), QString::fromLatin1(annot::kEditNoteStep));
}

QRectF PopupWidget::iconOnViewport() const {
    if (view_.pageViewportRect(ref_.page).isEmpty() || props_.rect.empty()) {
        return {};
    }
    return view_.pageTransform(ref_.page).mapRect(popupQRect(props_.rect));
}

void PopupWidget::reposition() {
    const QRectF pageOnScreen = view_.pageViewportRect(ref_.page);
    if (pageOnScreen.isEmpty()) {
        hide();
        return;
    }
    const QTransform toView = view_.pageTransform(ref_.page);
    QPointF anchor;
    if (props_.hasPopup()) {
        anchor = toView.mapRect(popupQRect(props_.popupRect)).topLeft();
    } else {
        const QRectF icon = toView.mapRect(popupQRect(props_.rect));
        anchor = QPointF(icon.right() + Theme::sizes().grid * 2, icon.top());
    }
    const QRect area = view_.viewport()->rect();
    const int x =
        std::clamp(static_cast<int>(anchor.x()), area.left(), std::max(area.left(), area.right() - width()));
    const int y =
        std::clamp(static_cast<int>(anchor.y()), area.top(), std::max(area.top(), area.bottom() - height()));
    move(x, y);
    show();
    raise();
}

// ---------------------------------------------------------------- PopupLayer

PopupLayer& PopupLayer::of(PageView& view) {
    if (auto* layer = view.findChild<PopupLayer*>(QString(), Qt::FindDirectChildrenOnly)) {
        return *layer;
    }
    return *new PopupLayer(view);
}

PopupLayer::PopupLayer(PageView& view) : QObject(&view), view_(view) {
    setObjectName(QStringLiteral("commentPopups"));
    // Слой живёт, пока жив вид (ребёнок), — из стека слоёв не снимается, как ToolController.
    view_.overlays().add(this, 50);
    view_.viewport()->installEventFilter(this);
    connect(view_.horizontalScrollBar(), &QScrollBar::valueChanged, this, &PopupLayer::repositionAll);
    connect(view_.verticalScrollBar(), &QScrollBar::valueChanged, this, &PopupLayer::repositionAll);
    connect(&view_, &PageView::zoomChanged, this, &PopupLayer::repositionAll);
    connect(&view_, &PageView::layoutChanged, this, &PopupLayer::repositionAll);
    if (DocumentSession* s = view_.document()) {
        connect(s, &DocumentSession::pageChanged, this, &PopupLayer::pageChanged);
    }
}

std::vector<PopupWidget*> PopupLayer::popups() const {
    std::vector<PopupWidget*> out;
    for (const QPointer<PopupWidget>& p : open_) {
        if (p != nullptr) {
            out.push_back(p.data());
        }
    }
    return out;
}

PopupWidget* PopupLayer::popup(const annot::AnnotRef& ref) const {
    for (PopupWidget* p : popups()) {
        if (p->ref() == ref) {
            return p;
        }
    }
    return nullptr;
}

void PopupLayer::open(const annot::AnnotRef& ref, bool focus) {
    DocumentSession* session = view_.document();
    if (session == nullptr || !ref.valid()) {
        return;
    }
    if (PopupWidget* existing = popup(ref)) {
        existing->reposition();
        if (focus) {
            existing->editor()->setFocus();
        }
        return;
    }
    auto* widget = new PopupWidget(view_, ref);
    widget->hide();
    open_.emplace_back(widget);
    connect(widget, &PopupWidget::closeRequested, this, [this](const annot::AnnotRef& r) { close(r); });
    const QPointer<PopupWidget> guard(widget);
    annot::props(*session, ref)
        .then(this, [this, guard, focus, ref](QFuture<std::optional<annot::AnnotProps>> f) {
            if (guard == nullptr) {
                return;
            }
            std::optional<annot::AnnotProps> props;
            try {
                props = f.result();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет ответа — окна нет
            }
            if (!props) {
                close(ref);
                return;
            }
            guard->setProps(*props);
            if (focus) {
                guard->editor()->setFocus();
                guard->editor()->moveCursor(QTextCursor::End);
            }
            view_.viewport()->update();
            emit opened(ref);
        });
}

void PopupLayer::close(const annot::AnnotRef& ref) {
    for (auto it = open_.begin(); it != open_.end();) {
        if (*it == nullptr || (*it)->ref() == ref) {
            if (*it != nullptr) {
                (*it)->flush();
                (*it)->hide();
                (*it)->deleteLater();
            }
            it = open_.erase(it);
        } else {
            ++it;
        }
    }
    view_.viewport()->update();
}

void PopupLayer::closeAll() {
    for (const QPointer<PopupWidget>& p : open_) {
        if (p != nullptr) {
            p->hide();
            p->deleteLater();
        }
    }
    open_.clear();
    view_.viewport()->update();
}

void PopupLayer::repositionAll() {
    for (PopupWidget* p : popups()) {
        p->reposition();
    }
}

void PopupLayer::pageChanged(int page) {
    DocumentSession* session = view_.document();
    if (session == nullptr) {
        return;
    }
    for (PopupWidget* p : popups()) {
        if (p->ref().page != page) {
            continue;
        }
        const annot::AnnotRef ref = p->ref();
        const QPointer<PopupWidget> guard(p);
        annot::props(*session, ref)
            .then(this, [this, guard, ref](QFuture<std::optional<annot::AnnotProps>> f) {
                if (guard == nullptr) {
                    return;
                }
                std::optional<annot::AnnotProps> props;
                try {
                    props = f.result();
                } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет ответа — закрыть
                }
                if (!props || props->type == annot::AnnotType::Unknown) {
                    close(ref);
                    return;
                }
                guard->setProps(*props);
                view_.viewport()->update();
            });
    }
}

void PopupLayer::paintViewport(QPainter& painter, const PageView& /*view*/) {
    if (open_.empty() || !Settings().get(settings::kCommentConnectorLines)) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (PopupWidget* p : popups()) {
        if (!p->isVisible()) {
            continue;
        }
        const QRectF icon = p->iconOnViewport();
        if (icon.isEmpty()) {
            continue;
        }
        const QRectF box = p->geometry();
        const QPointF from = icon.center();
        const QPointF to(std::clamp(from.x(), box.left(), box.right()),
                         std::clamp(from.y(), box.top(), box.bottom()));
        QPen pen(popupColor(p->props().color).darker(130), Theme::sizes().pageBorder, Qt::DotLine);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.drawLine(from, to);
    }
    painter.restore();
}

bool PopupLayer::eventFilter(QObject* watched, QEvent* event) {
    if (watched != view_.viewport()) {
        return QObject::eventFilter(watched, event);
    }
    if (event->type() == QEvent::Resize) {
        repositionAll();
    } else if (event->type() == QEvent::ToolTip && Settings().get(settings::kCommentPopupOnHover)) {
        auto* help = static_cast<QHelpEvent*>(event);
        showTip(help->globalPos(), help->pos());
        return true;
    }
    return QObject::eventFilter(watched, event);
}

// Подсказка «автор — текст» над комментарием (окно при наведении).
void PopupLayer::showTip(QPoint global, QPointF viewport) {
    DocumentSession* session = view_.document();
    const int page = view_.pageAtViewport(viewport);
    const quint64 request = ++tip_;
    if (session == nullptr || page < 0) {
        QToolTip::hideText();
        return;
    }
    const QPointF pt = view_.viewportToPage(page, viewport);
    annot::at(*session, page, PointF{static_cast<float>(pt.x()), static_cast<float>(pt.y())})
        .then(this, [this, request, global](QFuture<std::optional<annot::AnnotInfo>> f) {
            if (request != tip_) {
                return;
            }
            std::optional<annot::AnnotInfo> hit;
            try {
                hit = f.result();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет ответа — без подсказки
            }
            if (!hit || !annot::isComment(hit->type) || hit->contents.empty() || popup(hit->ref) != nullptr) {
                QToolTip::hideText();
                return;
            }
            const QString author = QString::fromStdString(hit->author).toHtmlEscaped();
            QString text = QString::fromStdString(hit->contents).toHtmlEscaped();
            text.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
            QToolTip::showText(global,
                               author.isEmpty() ? text : QStringLiteral("<b>%1</b><br>%2").arg(author, text),
                               view_.viewport());
        });
}

}  // namespace ot::ui
