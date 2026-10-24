// «Выделение» (V; эталон 3.2): текст потоком по строкам от точки до точки (через страницы — до конца первой,
// с начала последней, промежуточные целиком), Alt — прямоугольником (столбцы), двойной щелчок — слово,
// тройной — строка, Shift+щелчок — продолжить, Ctrl+A — всё, I-курсор над строкой текста. Слой текста —
// задачей DocumentSession::textPage (core/text/stext.h), выделение — core/text/selection.h; подсветка — слой
// выделения PageView. 2.2b: Ctrl+C — text/plain + text/html через ClipboardIo; клавиатурный курсор (ocr-41) —
// граница символа: щелчок ставит, Shift+стрелки ведут выделение от начала до курсора (через страницы — если
// их слой уже загружен), курсор виден, пока у вида фокус.
#include <QApplication>
#include <QFuture>

#include <algorithm>
#include <cmath>

#include "core/text/stext.h"
#include "services/clipboard_io.h"
#include "services/document_session.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/view/tools/view_tool.h"

namespace ot::ui {

namespace {

constexpr float kTextHoverTolerance = 1.0f;  // пункты: I-курсор чуть шире строки
constexpr double kCaretMarginPx = 24;        // поле вокруг курсора, к которому прокручивается вид

PointF selectPagePoint(QPointF p) { return {static_cast<float>(p.x()), static_cast<float>(p.y())}; }

float charLeft(const TextChar& c) { return std::min(c.quad.ul.x, c.quad.ll.x); }
float charRight(const TextChar& c) { return std::max(c.quad.ur.x, c.quad.lr.x); }

// x границы курсора на странице (после последнего символа — правая кромка).
float caretX(const TextPage& text, int boundary, bool trailing) {
    const auto& chars = text.chars();
    if (boundary > 0 && (trailing || boundary >= static_cast<int>(chars.size()))) {
        return charRight(chars[static_cast<std::size_t>(boundary - 1)]);
    }
    return charLeft(chars[static_cast<std::size_t>(boundary)]);
}

// Строка курсора: символа boundary, в конце (trailing, конец страницы) — предыдущего.
int caretLine(const TextPage& text, int boundary, bool trailing) {
    const auto& chars = text.chars();
    const int n = static_cast<int>(chars.size());
    const int index = (trailing || boundary >= n) ? boundary - 1 : boundary;
    return chars[static_cast<std::size_t>(std::clamp(index, 0, n - 1))].line;
}

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
    keyboard_ = false;
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
    if (event->matches(QKeySequence::Copy)) {
        return copySelection();
    }
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    const int key = event->key();
    if (mods == Qt::ShiftModifier &&
        (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down)) {
        moveCaret(key);
        return true;  // Shift+стрелки — всегда выделение, не прокрутка
    }
    return false;
}

bool SelectTool::copySelection() {
    if (!controller_.copyAllowed()) {
        return false;  // право /P «копирование» снято (2.5b)
    }
    std::vector<ClipboardIo::PageSelection> parts;
    for (const auto& [page, sel] : selection_) {
        const auto it = pages_.constFind(page);
        if (it != pages_.constEnd() && *it) {
            parts.emplace_back(it->get(), &sel);
        }
    }
    if (parts.empty()) {
        return false;
    }
    ClipboardIo::put(ClipboardIo::textMime(parts));
    return true;
}

// ---------------------------------------------------------------- клавиатурный курсор

void SelectTool::syncCaret() {
    if (keyboard_ || rectangle_) {
        return;
    }
    if (!selection_.empty()) {
        // Конец протягивания: вперёд — после последнего символа, назад — перед первым.
        const bool forward =
            anchor_.page < focus_.page ||
            (anchor_.page == focus_.page && (anchor_.pagePoint.y() < focus_.pagePoint.y() ||
                                             (anchor_.pagePoint.y() == focus_.pagePoint.y() &&
                                              anchor_.pagePoint.x() <= focus_.pagePoint.x())));
        if (forward) {
            const auto& [page, sel] = *selection_.rbegin();
            caret_ = {page, sel.chars.back() + 1, true};
        } else {
            const auto& [page, sel] = *selection_.begin();
            caret_ = {page, sel.chars.front(), false};
        }
        return;
    }
    if (anchor_.page < 0) {
        return;
    }
    const auto it = pages_.constFind(anchor_.page);
    if (it != pages_.constEnd() && *it && !(*it)->chars().empty()) {
        caret_ = {anchor_.page, closestBoundary(**it, selectPagePoint(anchor_.pagePoint)), false};
    }
}

SelectTool::Caret SelectTool::stepCaret(const Caret& from, int delta) {
    const TextPage* text = textPage(from.page);
    if (text == nullptr) {
        return from;
    }
    const int n = static_cast<int>(text->chars().size());
    const int b = from.boundary + delta;
    if (b < 0) {
        const TextPage* prev = textPage(from.page - 1);
        return prev != nullptr && !prev->chars().empty()
                   ? Caret{from.page - 1, static_cast<int>(prev->chars().size()), true}
                   : Caret{from.page, 0, false};
    }
    if (b > n) {
        const TextPage* next = textPage(from.page + 1);
        return next != nullptr && !next->chars().empty() ? Caret{from.page + 1, 0, false}
                                                         : Caret{from.page, n, true};
    }
    return {from.page, b, b == n};
}

SelectTool::Caret SelectTool::stepCaretLine(const Caret& from, int delta) {
    const TextPage* text = textPage(from.page);
    if (text == nullptr || text->chars().empty()) {
        return from;
    }
    const float x = caretX(*text, from.boundary, from.trailing);
    int page = from.page;
    int line = caretLine(*text, from.boundary, from.trailing) + delta;
    // Пустые строки пропускаются; за краем страницы — соседняя (если её слой загружен).
    while (true) {
        if (line < 0 || line >= static_cast<int>(text->lines().size())) {
            const TextPage* other = textPage(page + delta);
            if (other == nullptr || other->lines().empty()) {
                return from;
            }
            page += delta;
            text = other;
            line = delta > 0 ? 0 : static_cast<int>(text->lines().size()) - 1;
        }
        if (text->lines()[static_cast<std::size_t>(line)].charCount > 0) {
            break;
        }
        line += delta;
    }
    const TextLine& l = text->lines()[static_cast<std::size_t>(line)];
    const auto& chars = text->chars();
    Caret best{page, l.firstChar, false};
    float bestDistance = std::fabs(charLeft(chars[static_cast<std::size_t>(l.firstChar)]) - x);
    for (int i = l.firstChar + 1; i <= l.firstChar + l.charCount; ++i) {
        const bool end = i == l.firstChar + l.charCount;
        const float bx = end ? charRight(chars[static_cast<std::size_t>(i - 1)])
                             : charLeft(chars[static_cast<std::size_t>(i)]);
        const float d = std::fabs(bx - x);
        if (d < bestDistance) {
            bestDistance = d;
            best = {page, i, end};
        }
    }
    return best;
}

void SelectTool::selectBetween(const Caret& a, const Caret& b) {
    selection_.clear();
    const bool ordered = a.page < b.page || (a.page == b.page && a.boundary <= b.boundary);
    const Caret& from = ordered ? a : b;
    const Caret& to = ordered ? b : a;
    for (int page = from.page; page <= to.page; ++page) {
        const TextPage* text = textPage(page);
        if (text == nullptr) {
            continue;
        }
        const int first = page == from.page ? from.boundary : 0;
        const int last = page == to.page ? to.boundary - 1 : static_cast<int>(text->chars().size()) - 1;
        TextSelection sel = selectRange(*text, first, last);
        if (!sel.empty()) {
            selection_.emplace(page, std::move(sel));
        }
    }
}

bool SelectTool::moveCaret(int key) {
    if (!keyboard_) {
        // Начало: от выделения мышью (его начало — якорь, конец — курсор), от курсора щелчка или с начала
        // текущей страницы.
        if (!selection_.empty() && !rectangle_ && caret_.valid()) {
            const bool caretAtEnd = caret_.page == selection_.rbegin()->first &&
                                    caret_.boundary == selection_.rbegin()->second.chars.back() + 1;
            keyAnchor_ =
                caretAtEnd ? Caret{selection_.begin()->first, selection_.begin()->second.chars.front()}
                           : Caret{selection_.rbegin()->first, selection_.rbegin()->second.chars.back() + 1};
        } else if (caret_.valid() && selection_.empty()) {
            keyAnchor_ = caret_;
        } else {
            const int page = view_.currentPage();
            const TextPage* text = textPage(page);
            if (text == nullptr) {
                return false;  // слой страницы придёт — следующее нажатие начнёт
            }
            caret_ = keyAnchor_ = Caret{page, 0, false};
        }
        keyboard_ = true;
        rectangle_ = false;
        anchor_ = focus_ = Point{};
        selectAllPending_ = false;
    }
    switch (key) {
        case Qt::Key_Left:
            caret_ = stepCaret(caret_, -1);
            break;
        case Qt::Key_Right:
            caret_ = stepCaret(caret_, 1);
            break;
        case Qt::Key_Up:
            caret_ = stepCaretLine(caret_, -1);
            break;
        default:
            caret_ = stepCaretLine(caret_, 1);
            break;
    }
    selectBetween(keyAnchor_, caret_);
    publish();
    controller_.emitSelectionChanged();
    if (const auto r = caretRect()) {
        view_.ensureVisible(caret_.page, *r, kCaretMarginPx);
    }
    return true;
}

std::optional<RectF> SelectTool::caretRect() const {
    if (!caret_.valid()) {
        return std::nullopt;
    }
    const auto it = pages_.constFind(caret_.page);
    if (it == pages_.constEnd() || !*it || (*it)->chars().empty()) {
        return std::nullopt;
    }
    const TextPage& text = **it;
    const float x = caretX(text, caret_.boundary, caret_.trailing);
    const RectF& line =
        text.lines()[static_cast<std::size_t>(caretLine(text, caret_.boundary, caret_.trailing))].bbox;
    return RectF{x, line.y0, x, line.y1};
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
    syncCaret();
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
    if (keyboard_) {
        selectBetween(keyAnchor_, caret_);  // промежуточная страница выделения клавишами пришла
        publish();
        return;
    }
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
    keyboard_ = false;
    caret_ = Caret{};
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
    caret_ = keyAnchor_ = Caret{};
    keyboard_ = false;
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
    if (const auto r = caretRect(); r && view_.hasFocus() && !pressing_) {
        // Курсор выделения — на бумаге (всегда светлой), поэтому цвет accent, а не текст темы.
        const QTransform t = view_.pageTransform(caret_.page);
        QPen caretPen(Theme::colors().accent.color(), Theme::sizes().focusRing);
        caretPen.setCapStyle(Qt::FlatCap);
        painter.setPen(caretPen);
        painter.drawLine(t.map(QPointF(r->x0, r->y0)), t.map(QPointF(r->x1, r->y1)));
    }
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
