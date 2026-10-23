// «Выделение» (V; эталон 3.2): текст потоком по строкам от точки до точки (через страницы — до конца первой,
// с начала последней, промежуточные целиком), Alt — прямоугольником (столбцы), двойной щелчок — слово,
// тройной — строка, Shift+щелчок — продолжить, Ctrl+A — всё, I-курсор над строкой текста. Слой текста —
// задачей DocumentSession::textPage (core/text/stext.h), выделение — core/text/selection.h; подсветка — слой
// выделения PageView. Копирование, HTML и клавиатурный курсор — сессия 2.2b.
#include <QApplication>
#include <QFuture>

#include <algorithm>

#include "core/text/stext.h"
#include "services/document_session.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/view/tools/view_tool.h"

namespace ot::ui {

namespace {

constexpr float kTextHoverTolerance = 1.0f;  // пункты: I-курсор чуть шире строки

PointF selectPagePoint(QPointF p) { return {static_cast<float>(p.x()), static_cast<float>(p.y())}; }

}  // namespace

QString SelectTool::title() const { return QCoreApplication::translate("ot::ui::SelectTool", "Выделение"); }

const TextPage* SelectTool::textPage(int page) {
    if (page < 0 || page >= view_.pageCount()) {
        return nullptr;
    }
    const auto it = pages_.constFind(page);
    if (it != pages_.constEnd()) {
        return it->get();
    }
    DocumentSession* session = view_.document();
    if (session == nullptr || !session->isOpen() || requested_.contains(page)) {
        return nullptr;
    }
    requested_.insert(page);
    QFuture<std::shared_ptr<const TextPage>> future =
        session->textPage(page, TextOptions{}, TaskPriority::High);
    future.then(&controller_, [this, page](QFuture<std::shared_ptr<const TextPage>> done) {
        if (!requested_.remove(page)) {
            return;  // слои сброшены (документ сменился) — ответ устарел
        }
        try {
            if (std::shared_ptr<const TextPage> text = done.result()) {
                pages_.insert(page, std::move(text));
                textArrived(page);
            }
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): без слоя — страница без выделения
        }
    });
    return nullptr;
}

void SelectTool::forgetPages() {
    pages_.clear();
    requested_.clear();
    clearSelection();
}

void SelectTool::forgetPage(int page) {
    pages_.remove(page);
    requested_.remove(page);
    if (selection_.erase(page) > 0) {
        publish();
    }
}

SelectTool::Point SelectTool::locate(QPointF viewportPoint) const {
    Point p;
    int page = view_.pageAtViewport(viewportPoint);
    if (page < 0) {
        page = view_.pageLayout().nearestPage(view_.viewportToContent(viewportPoint), view_.currentRow());
    }
    if (page < 0) {
        return p;
    }
    p.page = page;
    p.pagePoint = view_.viewportToPage(page, viewportPoint);
    return p;
}

bool SelectTool::mousePress(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return false;
    }
    const QPointF pos = event->position();
    const bool near = lastClick_.isValid() && lastClick_.elapsed() < QApplication::doubleClickInterval() &&
                      (pos - lastClickPos_).manhattanLength() < QApplication::startDragDistance();
    clicks_ = near ? std::min(clicks_ + 1, 3) : 1;
    lastClick_.restart();
    lastClickPos_ = pos;
    startAt(event, clicks_);
    return true;
}

bool SelectTool::mouseDoubleClick(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return false;
    }
    // Qt шлёт двойной щелчок вместо второго нажатия (или вслед за ним) — это второй щелчок, не третий.
    clicks_ = std::max(clicks_, 2);
    lastClick_.restart();
    lastClickPos_ = event->position();
    startAt(event, clicks_);
    return true;
}

void SelectTool::startAt(QMouseEvent* event, int clicks) {
    const Qt::KeyboardModifiers mods = event->modifiers();
    pressing_ = true;
    selectAllPending_ = false;
    rectangle_ = (mods & Qt::AltModifier) != 0 && clicks == 1;
    snap_ = clicks >= 3 ? SnapMode::Lines : (clicks == 2 ? SnapMode::Words : SnapMode::Chars);
    if (rectangle_) {
        rectStart_ = rectEnd_ = event->position();
        anchor_ = focus_ = Point{};
        recompute();
        return;
    }
    const Point p = locate(event->position());
    if ((mods & Qt::ShiftModifier) != 0 && anchor_.page >= 0 && clicks == 1) {
        focus_ = p;
    } else {
        anchor_ = focus_ = p;
    }
    textPage(p.page);
    recompute();
}

bool SelectTool::mouseMove(QMouseEvent* event) {
    if (!pressing_) {
        // Наведение: слой страницы под курсором — заранее (I-курсор, быстрый старт выделения).
        textPage(view_.pageAtViewport(event->position()));
        return false;
    }
    if (rectangle_) {
        rectEnd_ = event->position();
    } else {
        const Point p = locate(event->position());
        if (p.page < 0) {
            return true;
        }
        focus_ = p;
    }
    recompute();
    return true;
}

bool SelectTool::mouseRelease(QMouseEvent* event) {
    if (!pressing_ || event->button() != Qt::LeftButton) {
        return false;
    }
    pressing_ = false;
    view_.viewport()->update();
    controller_.emitSelectionChanged();
    return true;
}

bool SelectTool::keyPress(QKeyEvent* event) {
    if (event->matches(QKeySequence::SelectAll)) {
        selectAll();
        return true;
    }
    return false;
}

void SelectTool::recompute() {
    selection_.clear();
    if (rectangle_) {
        const QRectF band = QRectF(rectStart_, rectEnd_).normalized();
        for (const int page : view_.visiblePages()) {
            const QRectF onScreen = view_.pageViewportRect(page).intersected(band);
            if (onScreen.isEmpty()) {
                continue;
            }
            const TextPage* text = textPage(page);
            if (text == nullptr) {
                continue;
            }
            const QRectF area = view_.pageTransform(page).inverted().mapRect(onScreen);
            TextSelection sel = selectRectangle(
                *text, RectF{static_cast<float>(area.left()), static_cast<float>(area.top()),
                             static_cast<float>(area.right()), static_cast<float>(area.bottom())});
            if (!sel.empty()) {
                selection_.emplace(page, std::move(sel));
            }
        }
        publish();
        return;
    }
    if (anchor_.page < 0 || focus_.page < 0) {
        publish();
        return;
    }
    Point a = anchor_;
    Point b = focus_;
    if (b.page < a.page) {
        std::swap(a, b);
    }
    for (int page = a.page; page <= b.page; ++page) {
        const TextPage* text = textPage(page);
        if (text == nullptr) {
            continue;
        }
        const RectF bounds = text->bounds();
        const PointF start{bounds.x0 - 1, bounds.y0 - 1};
        const PointF end{bounds.x1 + 1, bounds.y1 + 1};
        TextSelection sel;
        if (a.page == b.page) {
            sel = selectStream(*text, selectPagePoint(a.pagePoint), selectPagePoint(b.pagePoint), snap_);
        } else if (page == a.page) {
            sel = selectStream(*text, selectPagePoint(a.pagePoint), end, snap_);
        } else if (page == b.page) {
            sel = selectStream(*text, start, selectPagePoint(b.pagePoint), snap_);
        } else {
            sel = ot::selectAll(*text);
        }
        if (!sel.empty()) {
            selection_.emplace(page, std::move(sel));
        }
    }
    publish();
}

void SelectTool::publish() {
    QuadOverlay& overlay = view_.selectionOverlay();
    overlay.clear();
    for (const auto& [page, sel] : selection_) {
        overlay.setQuads(page, sel.quads);
    }
    view_.viewport()->update();
}

void SelectTool::textArrived(int page) {
    if (selectAllPending_) {
        if (const TextPage* text = textPage(page)) {
            TextSelection sel = ot::selectAll(*text);
            if (!sel.empty()) {
                selection_[page] = std::move(sel);
                publish();
                controller_.emitSelectionChanged();
            }
        }
        return;
    }
    const bool inStream = anchor_.page >= 0 && page >= std::min(anchor_.page, focus_.page) &&
                          page <= std::max(anchor_.page, focus_.page);
    if (rectangle_ || inStream) {
        recompute();
        if (!pressing_) {
            controller_.emitSelectionChanged();
        }
    }
}

void SelectTool::selectAll() {
    anchor_ = focus_ = Point{};
    rectangle_ = false;
    pressing_ = false;
    selection_.clear();
    selectAllPending_ = true;
    // Постраничный режим — страницы на экране; непрерывный — весь документ (слои приходят по мере
    // извлечения).
    std::vector<int> pages;
    if (view_.pageLayout().paged()) {
        pages = view_.visiblePages();
    } else {
        for (int i = 0; i < view_.pageCount(); ++i) {
            pages.push_back(i);
        }
    }
    for (const int page : pages) {
        if (const TextPage* text = textPage(page)) {
            TextSelection sel = ot::selectAll(*text);
            if (!sel.empty()) {
                selection_.emplace(page, std::move(sel));
            }
        }
    }
    publish();
    controller_.emitSelectionChanged();
}

void SelectTool::clearSelection() {
    const bool had = !selection_.empty();
    selection_.clear();
    anchor_ = focus_ = Point{};
    selectAllPending_ = false;
    rectangle_ = false;
    publish();
    if (had) {
        controller_.emitSelectionChanged();
    }
}

bool SelectTool::cancel() {
    if (!pressing_) {
        return false;
    }
    pressing_ = false;
    clearSelection();
    return true;
}

QString SelectTool::selectedText() const {
    QStringList parts;
    for (const auto& [page, sel] : selection_) {
        const auto it = pages_.constFind(page);
        if (it != pages_.constEnd() && *it) {
            parts << QString::fromStdString(toPlainText(**it, sel));
        }
    }
    return parts.join(QLatin1Char('\n'));
}

void SelectTool::paintOverlay(QPainter& painter) {
    if (!pressing_ || !rectangle_) {
        return;
    }
    QPen pen(Theme::colors().accent.color(), Theme::sizes().pageBorder, Qt::DashLine);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(rectStart_, rectEnd_).normalized());
}

QCursor SelectTool::cursor(QPointF point, Qt::KeyboardModifiers modifiers) const {
    if ((modifiers & Qt::AltModifier) != 0) {
        return QCursor(Qt::CrossCursor);  // столбцы
    }
    const int page = view_.pageAtViewport(point);
    const auto it = page >= 0 ? pages_.constFind(page) : pages_.constEnd();
    if (it == pages_.constEnd() || !*it) {
        return QCursor(Qt::ArrowCursor);
    }
    const QPointF pt = view_.viewportToPage(page, point);
    const bool overText = (*it)->lineAt(selectPagePoint(pt), kTextHoverTolerance) >= 0;
    return QCursor(overText ? Qt::IBeamCursor : Qt::ArrowCursor);
}

}  // namespace ot::ui
