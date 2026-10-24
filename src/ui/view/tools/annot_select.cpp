// «Выбор объектов» (план 2.2b; эталон 3.4 «Выделение и правка на странице»; основа comment-08): аннотации
// страницы через минимальное ядро 2.2 (services/annot_ops.h). Щелчок — хит-тест задачей документа (annotAt):
// пока ответа нет, нажатие ждёт (Drag::Pending) — ответ решает, тянуть ли попавшую аннотацию или вести рамку
// выбора; отпущенная до ответа кнопка доделывается в ответе. По выбранной рамке и её ручкам — сразу, без
// задачи (рамки известны). Рамка выбора берёт списки страниц (annot::list) — заранее при наведении.
// Перемещение и масштаб — предпросмотр рамкой, по отпусканию — setRects одной операцией журнала; правка
// страницы (и undo) приходит pageChanged — рамки выбранных читаются заново, исчезнувшие снимаются.
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QFuture>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMimeData>

#include <algorithm>
#include <cmath>

#include "services/annot_ops.h"
#include "services/document_session.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/view/tools/view_tool.h"

namespace ot::ui {

namespace {

constexpr double kHitTolerancePx = 3.0;  // щелчок чуть мимо рамки — ещё попадание (px экрана)
constexpr double kHandleSlopPx = 3.0;    // ручка ловится шире своего квадрата
constexpr double kMinSizePx = 4.0;       // рамку не сжать меньше (px экрана)

QRectF annotQRect(const RectF& r) { return QRectF(QPointF(r.x0, r.y0), QPointF(r.x1, r.y1)); }

RectF annotRectF(const QRectF& r) {
    const QRectF n = r.normalized();
    return {static_cast<float>(n.left()), static_cast<float>(n.top()), static_cast<float>(n.right()),
            static_cast<float>(n.bottom())};
}

bool annotSelectable(const annot::AnnotInfo& info) {
    return info.type != annot::AnnotType::Popup && !info.hiddenOnScreen();
}

QPointF annotHandlePoint(const QRectF& r, int handle) {
    const QPointF c = r.center();
    switch (handle) {
        case AnnotSelectTool::TopLeft:
            return r.topLeft();
        case AnnotSelectTool::Top:
            return {c.x(), r.top()};
        case AnnotSelectTool::TopRight:
            return r.topRight();
        case AnnotSelectTool::Right:
            return {r.right(), c.y()};
        case AnnotSelectTool::BottomRight:
            return r.bottomRight();
        case AnnotSelectTool::Bottom:
            return {c.x(), r.bottom()};
        case AnnotSelectTool::BottomLeft:
            return r.bottomLeft();
        default:
            return {r.left(), c.y()};
    }
}

bool handleMovesLeft(int h) {
    return h == AnnotSelectTool::TopLeft || h == AnnotSelectTool::Left || h == AnnotSelectTool::BottomLeft;
}
bool handleMovesRight(int h) {
    return h == AnnotSelectTool::TopRight || h == AnnotSelectTool::Right || h == AnnotSelectTool::BottomRight;
}
bool handleMovesTop(int h) {
    return h == AnnotSelectTool::TopLeft || h == AnnotSelectTool::Top || h == AnnotSelectTool::TopRight;
}
bool handleMovesBottom(int h) {
    return h == AnnotSelectTool::BottomLeft || h == AnnotSelectTool::Bottom ||
           h == AnnotSelectTool::BottomRight;
}

Qt::CursorShape annotHandleCursor(int h) {
    switch (h) {
        case AnnotSelectTool::TopLeft:
        case AnnotSelectTool::BottomRight:
            return Qt::SizeFDiagCursor;
        case AnnotSelectTool::TopRight:
        case AnnotSelectTool::BottomLeft:
            return Qt::SizeBDiagCursor;
        case AnnotSelectTool::Top:
        case AnnotSelectTool::Bottom:
            return Qt::SizeVerCursor;
        default:
            return Qt::SizeHorCursor;
    }
}

}  // namespace

QString AnnotSelectTool::title() const {
    return QCoreApplication::translate("ot::ui::AnnotSelectTool", "Выбор объектов");
}

void AnnotSelectTool::activate() {
    for (const int page : view_.visiblePages()) {
        annotList(page);
    }
}

void AnnotSelectTool::deactivate() {
    cancel();
    clearSelection();
}

// ---------------------------------------------------------------- выбор

bool AnnotSelectTool::isSelected(const AnnotRef& ref) const {
    return std::any_of(selected_.begin(), selected_.end(),
                       [&](const AnnotInfo& info) { return info.ref == ref; });
}

void AnnotSelectTool::setSelection(std::vector<AnnotInfo> annots) {
    selected_.clear();
    for (AnnotInfo& info : annots) {
        if (info.ref.valid() && !isSelected(info.ref)) {
            selected_.push_back(std::move(info));
        }
    }
    publish();
    selectionChanged();
}

void AnnotSelectTool::clearSelection() {
    bands_.clear();
    refresh_.clear();
    if (selected_.empty()) {
        publish();
        return;
    }
    selected_.clear();
    publish();
    selectionChanged();
}

void AnnotSelectTool::deleteSelection() {
    DocumentSession* session = view_.document();
    if (session == nullptr || selected_.empty() || !controller_.editable()) {
        return;  // только чтение — удалять нельзя
    }
    cancel();
    std::vector<AnnotRef> refs;
    refs.reserve(selected_.size());
    for (const AnnotInfo& info : selected_) {
        if (!info.locked()) {
            refs.push_back(info.ref);  // «Заблокировано» не удаляется
        }
    }
    if (refs.empty()) {
        return;
    }
    annot::remove(*session, std::move(refs));  // ошибка — taskFailed сессии
    clearSelection();
}

void AnnotSelectTool::selectionChanged() { controller_.emitAnnotSelectionChanged(); }

void AnnotSelectTool::publish() {
    RectOverlay& overlay = view_.annotationOverlay();
    overlay.clear();
    std::map<int, std::vector<RectF>> byPage;
    for (const AnnotInfo& info : selected_) {
        byPage[info.ref.page].push_back(draggedRect(info));
    }
    for (auto& [page, rects] : byPage) {
        overlay.setRects(page, std::move(rects));
    }
    view_.viewport()->update();
}

// ---------------------------------------------------------------- геометрия

std::optional<QRectF> AnnotSelectTool::frameOf(const AnnotInfo& info) const {
    if (view_.pageViewportRect(info.ref.page).isEmpty()) {
        return std::nullopt;  // страница не на экране (постраничный режим)
    }
    return view_.pageTransform(info.ref.page).mapRect(annotQRect(draggedRect(info)));
}

std::vector<QRectF> AnnotSelectTool::frames() const {
    std::vector<QRectF> result;
    for (const AnnotInfo& info : selected_) {
        if (const auto frame = frameOf(info)) {
            result.push_back(*frame);
        }
    }
    return result;
}

std::optional<AnnotSelectTool::Handle> AnnotSelectTool::handleAt(QPointF point) const {
    if (selected_.size() != 1 || !movable(selected_.front())) {
        return std::nullopt;
    }
    const auto frame = frameOf(selected_.front());
    if (!frame) {
        return std::nullopt;
    }
    const double reach = Theme::sizes().handle / 2.0 + kHandleSlopPx;
    std::optional<Handle> best;
    double bestDistance = reach;
    for (int h = 0; h < kHandleCount; ++h) {
        const QPointF d = point - annotHandlePoint(*frame, h);
        const double distance = std::max(std::fabs(d.x()), std::fabs(d.y()));
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = static_cast<Handle>(h);
        }
    }
    return best;
}

bool AnnotSelectTool::selectedAt(QPointF point) const {
    return std::any_of(selected_.begin(), selected_.end(), [&](const AnnotInfo& info) {
        const auto frame = frameOf(info);
        return frame && frame->adjusted(-kHitTolerancePx, -kHitTolerancePx, kHitTolerancePx, kHitTolerancePx)
                            .contains(point);
    });
}

bool AnnotSelectTool::moved() const {
    return (current_ - press_.viewport).manhattanLength() >= QApplication::startDragDistance();
}

RectF AnnotSelectTool::draggedRect(const AnnotInfo& info) const {
    if ((drag_ != Drag::Move && drag_ != Drag::Resize) || !moved() || !movable(info)) {
        return info.rect;
    }
    const QTransform toView = view_.pageTransform(info.ref.page);
    const QRectF screen = toView.mapRect(annotQRect(info.rect));
    const QPointF delta = current_ - press_.viewport;
    QRectF next;
    if (drag_ == Drag::Move) {
        next = screen.translated(delta);
    } else {
        double l = screen.left();
        double t = screen.top();
        double r = screen.right();
        double b = screen.bottom();
        // Двигаемая сторона упирается в противоположную: рамка не выворачивается и не меньше kMinSizePx.
        if (handleMovesLeft(handle_)) {
            l = std::min(l + delta.x(), r - kMinSizePx);
        } else if (handleMovesRight(handle_)) {
            r = std::max(r + delta.x(), l + kMinSizePx);
        }
        if (handleMovesTop(handle_)) {
            t = std::min(t + delta.y(), b - kMinSizePx);
        } else if (handleMovesBottom(handle_)) {
            b = std::max(b + delta.y(), t + kMinSizePx);
        }
        next = QRectF(QPointF(l, t), QPointF(r, b));
    }
    QRectF onPage = toView.inverted().mapRect(next);
    if (drag_ == Drag::Move) {
        // Перемещение — в пределах страницы (если рамка в неё помещается).
        const QSizeF page = view_.pageSizePoints(info.ref.page);
        if (onPage.width() <= page.width()) {
            onPage.translate(std::max(0.0, -onPage.left()) - std::max(0.0, onPage.right() - page.width()), 0);
        }
        if (onPage.height() <= page.height()) {
            onPage.translate(0,
                             std::max(0.0, -onPage.top()) - std::max(0.0, onPage.bottom() - page.height()));
        }
    }
    return annotRectF(onPage);
}

// ---------------------------------------------------------------- мышь и клавиши

bool AnnotSelectTool::mousePress(QMouseEvent* event) {
    const Qt::MouseButton button = event->button();
    if (button != Qt::LeftButton && button != Qt::RightButton) {
        return false;
    }
    DocumentSession* session = view_.document();
    if (session == nullptr || !session->isOpen()) {
        return false;
    }
    const QPointF pos = event->position();
    press_ =
        Press{pos, event->globalPosition().toPoint(), view_.pageAtViewport(pos), button, event->modifiers()};
    current_ = pos;
    pressing_ = true;
    const bool shift = (event->modifiers() & Qt::ShiftModifier) != 0;
    if (button == Qt::LeftButton) {
        if (const auto handle = handleAt(pos)) {
            drag_ = Drag::Resize;
            handle_ = *handle;
            return true;
        }
        if (!shift && selectedAt(pos)) {
            drag_ = Drag::Move;
            return true;
        }
        if (press_.page < 0) {
            drag_ = Drag::Marquee;  // мимо страниц — аннотаций там нет
            return true;
        }
    } else {
        if (selectedAt(pos)) {
            pressing_ = false;
            showContextMenu(press_.global);
            return true;
        }
        if (press_.page < 0) {
            pressing_ = false;
            return true;
        }
    }
    drag_ = Drag::Pending;
    const quint64 request = ++request_;
    const QPointF pt = view_.viewportToPage(press_.page, pos);
    const auto tolerance = static_cast<float>(kHitTolerancePx / std::max(0.01f, view_.zoom()));
    annot::at(*session, press_.page, PointF{static_cast<float>(pt.x()), static_cast<float>(pt.y())},
              tolerance)
        .then(&controller_, [this, request](QFuture<std::optional<AnnotInfo>> done) {
            std::optional<AnnotInfo> hit;
            try {
                hit = done.result();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет ответа — как мимо
            }
            hitArrived(hit, request);
        });
    return true;
}

void AnnotSelectTool::hitArrived(const std::optional<AnnotInfo>& found, quint64 request) {
    if (request != request_ || drag_ != Drag::Pending) {
        return;  // нажатие брошено (Esc, смена документа) — ответ устарел
    }
    // Скрытые показом/скрытием комментариев — как пустое место.
    const std::optional<AnnotInfo> hit = found && hiddenType(found->type) ? std::nullopt : found;
    const bool shift = (press_.modifiers & Qt::ShiftModifier) != 0;
    if (press_.button == Qt::RightButton) {
        drag_ = Drag::None;
        if (!hit) {
            clearSelection();
            return;
        }
        if (!isSelected(hit->ref)) {
            setSelection({*hit});
        }
        showContextMenu(press_.global);
        return;
    }
    if (hit) {
        if (shift) {
            // Shift+щелчок: добавить или убрать, без перетаскивания.
            drag_ = Drag::None;
            const auto it = std::find_if(selected_.begin(), selected_.end(),
                                         [&](const AnnotInfo& info) { return info.ref == hit->ref; });
            if (it != selected_.end()) {
                selected_.erase(it);
            } else {
                selected_.push_back(*hit);
            }
            publish();
            selectionChanged();
            return;
        }
        if (!isSelected(hit->ref)) {
            setSelection({*hit});
        }
        drag_ = Drag::Move;
        if (!pressing_) {
            finishDrag();  // кнопку отпустили до ответа
        } else {
            publish();
        }
        return;
    }
    drag_ = Drag::Marquee;
    if (!pressing_) {
        finishMarquee();
    } else {
        view_.viewport()->update();
    }
}

bool AnnotSelectTool::mouseMove(QMouseEvent* event) {
    if (!pressing_) {
        annotList(view_.pageAtViewport(event->position()));  // заранее — для рамки выбора
        return false;
    }
    if (press_.button != Qt::LeftButton) {
        return true;
    }
    current_ = event->position();
    if (drag_ == Drag::Move || drag_ == Drag::Resize) {
        publish();
    } else if (drag_ == Drag::Marquee) {
        const QRectF band = QRectF(press_.viewport, current_).normalized();
        for (const int page : view_.visiblePages()) {
            if (view_.pageViewportRect(page).intersects(band)) {
                annotList(page);
            }
        }
        view_.viewport()->update();
    }
    return true;
}

bool AnnotSelectTool::mouseRelease(QMouseEvent* event) {
    if (!pressing_ || event->button() != press_.button) {
        return false;
    }
    pressing_ = false;
    if (press_.button != Qt::LeftButton) {
        return true;
    }
    current_ = event->position();
    switch (drag_) {
        case Drag::Move:
        case Drag::Resize:
            finishDrag();
            break;
        case Drag::Marquee:
            finishMarquee();
            break;
        default:
            break;  // Pending — доделает ответ хит-теста
    }
    return true;
}

void AnnotSelectTool::finishDrag() {
    DocumentSession* session = view_.document();
    if (!moved() || session == nullptr || !controller_.editable()) {
        drag_ = Drag::None;  // только чтение — рамки остаются на месте
        publish();
        return;
    }
    std::vector<annot::RectEdit> edits;
    edits.reserve(selected_.size());
    for (AnnotInfo& info : selected_) {
        if (movable(info)) {
            const RectF rect = draggedRect(info);
            edits.push_back({info.ref, rect});
        }
    }
    drag_ = Drag::None;
    if (edits.empty()) {
        publish();  // разметка и заблокированные не двигаются
        return;
    }
    // До ответа сессии рамки — новые; pageChanged принесёт настоящие (у NoZoom-иконок рамка — /BBox вида).
    for (const annot::RectEdit& e : edits) {
        for (AnnotInfo& info : selected_) {
            if (info.ref == e.ref) {
                info.rect = e.rect;
            }
        }
    }
    annot::setRects(*session, std::move(edits));  // ошибка — taskFailed сессии
    publish();
}

void AnnotSelectTool::finishMarquee() {
    drag_ = Drag::None;
    const bool shift = (press_.modifiers & Qt::ShiftModifier) != 0;
    if (!moved()) {
        if (!shift) {
            clearSelection();  // щелчок по пустому месту
        }
        view_.viewport()->update();
        return;
    }
    if (!shift) {
        selected_.clear();
    }
    bands_.clear();
    const QRectF band = QRectF(press_.viewport, current_).normalized();
    for (const int page : view_.visiblePages()) {
        const QRectF onScreen = view_.pageViewportRect(page).intersected(band);
        if (!onScreen.isEmpty()) {
            bands_[page] = annotRectF(view_.pageTransform(page).inverted().mapRect(onScreen));
        }
    }
    applyBands();
    publish();
    selectionChanged();
}

bool AnnotSelectTool::applyBands() {
    bool changed = false;
    for (auto it = bands_.begin(); it != bands_.end();) {
        const std::vector<AnnotInfo>* list = annotList(it->first);
        if (list == nullptr) {
            ++it;  // список придёт — listArrived доделает
            continue;
        }
        const QRectF band = annotQRect(it->second);
        for (const AnnotInfo& info : *list) {
            if (annotSelectable(info) && !hiddenType(info.type) &&
                annotQRect(info.rect).normalized().intersects(band) && !isSelected(info.ref)) {
                selected_.push_back(info);
                changed = true;
            }
        }
        it = bands_.erase(it);
    }
    return changed;
}

bool AnnotSelectTool::keyPress(QKeyEvent* event) {
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    if (event->key() == Qt::Key_Delete && mods == Qt::NoModifier && !selected_.empty()) {
        deleteSelection();
        return true;
    }
    if (event->matches(QKeySequence::Copy) && !selected_.empty()) {
        copySelection();
        return true;
    }
    if (event->matches(QKeySequence::Cut) && !selected_.empty()) {
        cutSelection();
        return true;
    }
    if (event->matches(QKeySequence::Paste) && canPaste()) {
        paste();
        return true;
    }
    return false;
}

void AnnotSelectTool::showContextMenu(QPoint global) {
    // Без своего цикла событий (popup, не exec): меню живёт само и удаляется при закрытии.
    auto* menu = new QMenu(view_.viewport());
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setObjectName(QStringLiteral("annotContextMenu"));
    fillContextMenu(*menu);
    menu->popup(global);
}

bool AnnotSelectTool::cancel() {
    if (drag_ == Drag::None) {
        return false;
    }
    drag_ = Drag::None;
    pressing_ = false;
    ++request_;
    publish();
    return true;
}

bool AnnotSelectTool::escape(bool cancelled) {
    if (cancelled) {
        return true;  // Esc бросил перетаскивание — выбор и инструмент остаются
    }
    if (!selected_.empty()) {
        clearSelection();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- буфер обмена, блокировка, меню (5.1)

bool AnnotSelectTool::movable(const AnnotInfo& info) noexcept {
    return !annot::isTextMarkup(info.type) && !info.locked();
}

void AnnotSelectTool::copySelection() {
    DocumentSession* session = view_.document();
    if (session == nullptr || selected_.empty()) {
        return;
    }
    std::vector<AnnotRef> refs;
    for (const AnnotInfo& info : selected_) {
        refs.push_back(info.ref);
    }
    const int page = selected_.front().ref.page;
    const QString source = QString::number(reinterpret_cast<quintptr>(session), 16);
    annot::copy(*session, std::move(refs)).then(&controller_, [source, page](QFuture<annot::AnnotClip> done) {
        annot::AnnotClip clip;
        try {
            clip = done.result();
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): не скопировалось — буфер прежний
            return;
        }
        if (clip.empty()) {
            return;
        }
        const QJsonObject o{
            {QStringLiteral("json"), QString::fromStdString(clip.json)},
            {QStringLiteral("pdf"),
             QString::fromLatin1(
                 QByteArray(clip.pdf.data(), static_cast<qsizetype>(clip.pdf.size())).toBase64())},
            {QStringLiteral("count"), clip.count},
            {QStringLiteral("source"), source},
            {QStringLiteral("page"), page}};
        auto* mime = new QMimeData;
        mime->setData(QLatin1String(kClipboardMime), QJsonDocument(o).toJson(QJsonDocument::Compact));
        QGuiApplication::clipboard()->setMimeData(mime);
    });
}

void AnnotSelectTool::cutSelection() {
    if (!controller_.editable() || selected_.empty()) {
        return;
    }
    copySelection();
    deleteSelection();  // задача удаления идёт после задачи копии — сессия выполняет по очереди
}

bool AnnotSelectTool::canPaste() const {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    return mime != nullptr && mime->hasFormat(QLatin1String(kClipboardMime)) && view_.document() != nullptr &&
           controller_.editable();
}

void AnnotSelectTool::paste() {
    DocumentSession* session = view_.document();
    if (!canPaste() || session == nullptr) {
        return;
    }
    const QJsonObject o =
        QJsonDocument::fromJson(QGuiApplication::clipboard()->mimeData()->data(QLatin1String(kClipboardMime)))
            .object();
    annot::AnnotClip clip;
    clip.json = o.value(QStringLiteral("json")).toString().toStdString();
    clip.pdf = QByteArray::fromBase64(o.value(QStringLiteral("pdf")).toString().toLatin1()).toStdString();
    clip.count = o.value(QStringLiteral("count")).toInt();
    const int page = std::clamp(view_.currentPage(), 0, std::max(0, view_.pageCount() - 1));
    const bool samePlace = o.value(QStringLiteral("source")).toString() ==
                               QString::number(reinterpret_cast<quintptr>(session), 16) &&
                           o.value(QStringLiteral("page")).toInt() == page;
    const PointF offset = samePlace ? PointF{kPasteOffset, kPasteOffset} : PointF{};
    annot::paste(*session, page, std::move(clip), offset)
        .then(&controller_, [this, page](QFuture<std::vector<AnnotRef>> done) {
            try {
                selectPasted(page, done.result());
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): ошибка — taskFailed сессии
            }
        });
}

void AnnotSelectTool::selectPasted(int page, std::vector<AnnotRef> refs) {
    DocumentSession* session = view_.document();
    if (session == nullptr || refs.empty()) {
        return;
    }
    const quint64 epoch = epoch_;
    annot::list(*session, page, TaskPriority::High)
        .then(&controller_, [this, epoch, refs = std::move(refs)](QFuture<std::vector<AnnotInfo>> done) {
            if (epoch != epoch_) {
                return;
            }
            std::vector<AnnotInfo> chosen;
            try {
                for (const AnnotInfo& info : done.result()) {
                    if (std::find(refs.begin(), refs.end(), info.ref) != refs.end()) {
                        chosen.push_back(info);
                    }
                }
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет списка — без выбора
            }
            if (controller_.tool() != ViewToolId::AnnotSelect) {
                controller_.setTool(ViewToolId::AnnotSelect);
            }
            setSelection(std::move(chosen));
        });
}

bool AnnotSelectTool::hiddenType(annot::AnnotType type) const noexcept {
    const int i = static_cast<int>(type);
    return i >= 0 && i < 32 && (hiddenTypes_ & (1u << i)) != 0;
}

void AnnotSelectTool::setHiddenTypes(std::uint32_t mask) {
    hiddenTypes_ = mask;
    const auto before = selected_.size();
    selected_.erase(std::remove_if(selected_.begin(), selected_.end(),
                                   [this](const AnnotInfo& info) { return hiddenType(info.type); }),
                    selected_.end());
    if (selected_.size() != before) {
        publish();
        selectionChanged();
    }
}

void AnnotSelectTool::setSelectionLocked(bool locked) {
    DocumentSession* session = view_.document();
    if (session == nullptr || selected_.empty() || !controller_.editable()) {
        return;
    }
    std::vector<AnnotRef> refs;
    for (AnnotInfo& info : selected_) {
        refs.push_back(info.ref);
        info.flags = locked ? (info.flags | annot::kAnnotLocked) : (info.flags & ~annot::kAnnotLocked);
    }
    annot::setFlags(*session, std::move(refs), locked ? annot::kAnnotLocked : 0,
                    locked ? 0 : annot::kAnnotLocked);
}

void AnnotSelectTool::fillContextMenu(QMenu& menu) {
    const bool editable = controller_.editable();
    const bool anyLocked =
        std::any_of(selected_.begin(), selected_.end(), [](const AnnotInfo& i) { return i.locked(); });
    const bool allLocked = !selected_.empty() && std::all_of(selected_.begin(), selected_.end(),
                                                             [](const AnnotInfo& i) { return i.locked(); });
    auto tr = [](const char* text) { return QCoreApplication::translate("ot::ui::AnnotSelectTool", text); };
    QAction* copy = menu.addAction(tr("Копировать"), [this] { copySelection(); });
    copy->setObjectName(QStringLiteral("annotCopy"));
    copy->setShortcut(QKeySequence::Copy);
    QAction* cut = menu.addAction(tr("Вырезать"), [this] { cutSelection(); });
    cut->setObjectName(QStringLiteral("annotCut"));
    cut->setShortcut(QKeySequence::Cut);
    cut->setEnabled(editable && !anyLocked);
    QAction* del = menu.addAction(tr("Удалить"), [this] { deleteSelection(); });
    del->setObjectName(QStringLiteral("annotDelete"));
    del->setShortcut(QKeySequence::Delete);
    del->setEnabled(editable && !anyLocked);
    menu.addSeparator();
    QAction* lock =
        menu.addAction(tr("Заблокировать"), [this, allLocked] { setSelectionLocked(!allLocked); });
    lock->setObjectName(QStringLiteral("annotLock"));
    lock->setCheckable(true);
    lock->setChecked(allLocked);
    lock->setEnabled(editable);
    controller_.emitAnnotContextMenu(&menu);  // пункты инструментов (5.1 — комментарии, 6.3 — поля)
}

bool AnnotSelectTool::wantsShortcut(const QKeyEvent* event) const {
    if ((event->matches(QKeySequence::Copy) || event->matches(QKeySequence::Cut)) && !selected_.empty()) {
        return true;
    }
    return event->matches(QKeySequence::Paste) && canPaste();
}

// ---------------------------------------------------------------- списки страниц

const std::vector<AnnotSelectTool::AnnotInfo>* AnnotSelectTool::annotList(int page) {
    if (page < 0 || page >= view_.pageCount()) {
        return nullptr;
    }
    const auto it = lists_.constFind(page);
    if (it != lists_.constEnd()) {
        return &*it;
    }
    DocumentSession* session = view_.document();
    if (session == nullptr || !session->isOpen() || listRequested_.contains(page)) {
        return nullptr;
    }
    listRequested_.insert(page);
    const quint64 generation = listGeneration_.value(page);
    const quint64 epoch = epoch_;
    annot::list(*session, page, TaskPriority::High)
        .then(&controller_, [this, page, generation, epoch](QFuture<std::vector<AnnotInfo>> done) {
            if (epoch != epoch_ || generation != listGeneration_.value(page) ||
                !listRequested_.remove(page)) {
                return;  // документ или страница изменились после запроса — придёт новый ответ
            }
            try {
                lists_.insert(page, done.result());
            } catch (const std::exception&) {
                lists_.insert(page, {});  // страница без списка — выбирать на ней нечего
            }
            listArrived(page);
        });
    return nullptr;
}

void AnnotSelectTool::listArrived(int page) {
    bool changed = false;
    if (refresh_.remove(page)) {
        const std::vector<AnnotInfo>& list = lists_[page];
        for (auto it = selected_.begin(); it != selected_.end();) {
            if (it->ref.page != page) {
                ++it;
                continue;
            }
            const auto found = std::find_if(list.begin(), list.end(),
                                            [&](const AnnotInfo& info) { return info.ref == it->ref; });
            if (found == list.end() || !annotSelectable(*found)) {
                it = selected_.erase(it);
                changed = true;
            } else {
                *it = *found;
                ++it;
            }
        }
        publish();
    }
    if (bands_.count(page) > 0 && applyBands()) {
        changed = true;
        publish();
    }
    if (changed) {
        selectionChanged();
    }
}

void AnnotSelectTool::documentChanged() {
    cancel();
    ++epoch_;
    lists_.clear();
    listRequested_.clear();
    listGeneration_.clear();
    clearSelection();
}

void AnnotSelectTool::pageChanged(int page) {
    lists_.remove(page);
    listRequested_.remove(page);
    ++listGeneration_[page];
    const bool affected = std::any_of(selected_.begin(), selected_.end(),
                                      [&](const AnnotInfo& info) { return info.ref.page == page; });
    if (affected) {
        refresh_.insert(page);
    }
    if (affected || bands_.count(page) > 0) {
        annotList(page);
    }
}

// ---------------------------------------------------------------- вид

void AnnotSelectTool::paintOverlay(QPainter& painter) {
    const auto& colors = Theme::colors();
    const auto& sizes = Theme::sizes();
    if (drag_ == Drag::Marquee && pressing_) {
        QPen pen(colors.accent.color(), sizes.pageBorder, Qt::DashLine);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(press_.viewport, current_).normalized());
    }
    if (selected_.size() != 1) {
        return;
    }
    const auto frame = frameOf(selected_.front());
    if (!frame) {
        return;
    }
    QPen pen(colors.accent.color(), sizes.pageBorder);
    pen.setCosmetic(true);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(pen);
    painter.setBrush(colors.surface.color());
    const double s = sizes.handle;
    for (int h = 0; h < kHandleCount; ++h) {
        const QPointF c = annotHandlePoint(*frame, h);
        painter.drawRect(QRectF(c - QPointF(s / 2, s / 2), QSizeF(s, s)));
    }
}

QCursor AnnotSelectTool::cursor(QPointF point, Qt::KeyboardModifiers /*modifiers*/) const {
    if (drag_ == Drag::Move) {
        return QCursor(Qt::SizeAllCursor);
    }
    const std::optional<Handle> handle =
        drag_ == Drag::Resize ? std::optional<Handle>(handle_) : handleAt(point);
    if (handle) {
        return QCursor(annotHandleCursor(*handle));
    }
    return QCursor(selectedAt(point) ? Qt::SizeAllCursor : Qt::ArrowCursor);
}

}  // namespace ot::ui
