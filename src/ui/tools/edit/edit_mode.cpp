#include "ui/tools/edit/edit_mode.h"

#include <QApplication>
#include <QCoreApplication>
#include <QToolTip>

#include <algorithm>

#include "services/document_session.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

namespace {

bool blockContains(const RectF& r, const PointF& p, float slack) {
    return p.x >= r.x0 - slack && p.x <= r.x1 + slack && p.y >= r.y0 - slack && p.y <= r.y1 + slack;
}

bool rectsMeet(const RectF& a, const RectF& b) {
    return a.valid() && b.valid() && a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1;
}

}  // namespace

EditMode::EditMode(PageView& view, ToolController& controller)
    : QObject(&controller), ViewTool(view, controller) {
    commitTimer_.setSingleShot(true);
    commitTimer_.setInterval(kCommitDelayMs);
    connect(&commitTimer_, &QTimer::timeout, this, &EditMode::commit);
    blink_.setInterval(kBlinkMs);
    connect(&blink_, &QTimer::timeout, this, [this] {
        caretOn_ = !caretOn_;
        view_.viewport()->update();
    });
    renderTimer_.setSingleShot(true);
    renderTimer_.setInterval(kRenderWaitMs);
    connect(&renderTimer_, &QTimer::timeout, this, [this] {
        renderWait_ = false;
        afterglow_.clear();
        view_.viewport()->update();
    });
}

EditMode::~EditMode() = default;

QString EditMode::title() const { return QCoreApplication::translate("ot::ui::EditMode", "Редактировать"); }

QString EditMode::reasonText(const QString& code) {
    static const std::pair<const char*, const char*> texts[] = {
        {"hidden", QT_TRANSLATE_NOOP("ot::ui::EditMode", "Текст скрытого слоя")},
        {"form",
         QT_TRANSLATE_NOOP("ot::ui::EditMode", "Текст внутри XObject-формы: её могут делить страницы")},
        {"syntax", QT_TRANSLATE_NOOP("ot::ui::EditMode", "Ошибка в контент-потоке страницы")},
        {"split", QT_TRANSLATE_NOOP("ot::ui::EditMode", "Оператор текста разрезан между потоками страницы")},
        {"type3", QT_TRANSLATE_NOOP("ot::ui::EditMode", "Шрифт Type 3: глифы нарисованы процедурами")},
        {"vertical", QT_TRANSLATE_NOOP("ot::ui::EditMode", "Вертикальный текст")},
        {"rotated", QT_TRANSLATE_NOOP("ot::ui::EditMode", "Повёрнутый или наклонный текст")},
        {"fontMissing", QT_TRANSLATE_NOOP("ot::ui::EditMode", "Шрифт не встроен и не найден в системе")},
        {"noWidths", QT_TRANSLATE_NOOP("ot::ui::EditMode", "У шрифта нет ширин символов")},
        {"noUnicode", QT_TRANSLATE_NOOP("ot::ui::EditMode", "У символов нет соответствия Unicode")},
        {"pattern", QT_TRANSLATE_NOOP("ot::ui::EditMode", "Текст залит узором")},
        {"clip", QT_TRANSLATE_NOOP("ot::ui::EditMode", "Текст служит обтравочным контуром")},
    };
    QString what;
    for (const auto& [id, text] : texts) {
        if (code == QLatin1String(id)) {
            what = QCoreApplication::translate("ot::ui::EditMode", text);
            break;
        }
    }
    if (what.isEmpty()) {
        what = QCoreApplication::translate("ot::ui::EditMode", "Блок не поддерживается редактором");
    }
    return QCoreApplication::translate("ot::ui::EditMode", "Только чтение: %1").arg(what);
}

// ---------------------------------------------------------------- вход в режим и выход

void EditMode::activate() {
    for (const int page : view_.visiblePages()) {
        requestContent(page);
    }
    view_.viewport()->update();
}

void EditMode::deactivate() {
    leaveBlock();
    pressing_ = false;
    QToolTip::hideText();
    view_.viewport()->update();
}

bool EditMode::cancel() {
    const bool was = pressing_;
    pressing_ = false;
    return was;
}

bool EditMode::escape(bool /*cancelled*/) {
    if (active_) {
        leaveBlock();
        return true;
    }
    return true;  // режим держит левая панель: Esc вне блока ничего не меняет
}

void EditMode::setLocked(const QString& reason) {
    if (reason == locked_) {
        return;
    }
    locked_ = reason;
    if (locked()) {
        leaveBlock();
    }
    view_.viewport()->update();
}

void EditMode::documentChanged() {
    ++epoch_;
    pages_.clear();
    generation_.clear();
    requested_.clear();
    commitTimer_.stop();
    committing_ = false;
    commitAgain_ = false;
    layoutBusy_ = false;
    layoutAgain_ = false;
    renderWait_ = false;
    detached_.clear();
    afterglow_.clear();
    dropActive();
}

void EditMode::pageChanged(int page) {
    generation_[page] = generation_.value(page) + 1;
    pages_.remove(page);
    requested_.remove(page);
    if (active_ && active_->page == page && !committing_) {
        // Страницу изменили не мы (undo, «Заменить все»): редактор блока устарел — копия бросается.
        dropActive();
    }
    if (controller_.tool() == ViewToolId::Edit) {
        requestContent(page);
    }
}

// ---------------------------------------------------------------- блоки страниц

const editing::PageContent* EditMode::content(int page) {
    const auto it = pages_.constFind(page);
    if (it != pages_.constEnd()) {
        return &*it;
    }
    requestContent(page);
    return nullptr;
}

void EditMode::requestContent(int page) {
    DocumentSession* session = view_.document();
    if (session == nullptr || !session->isOpen() || page < 0 || page >= view_.pageCount() ||
        pages_.contains(page) || requested_.contains(page)) {
        return;
    }
    requested_.insert(page);
    const quint64 generation = generation_.value(page);
    const quint64 epoch = epoch_;
    editing::pageContent(*session, page)
        .then(this, [this, page, generation, epoch](QFuture<editing::PageContent> done) {
            if (epoch != epoch_) {
                return;
            }
            editing::PageContent content;
            content.page = page;
            try {
                content = done.result();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): страница без блоков
            }
            contentArrived(page, generation, std::move(content));
        });
}

void EditMode::contentArrived(int page, quint64 generation, editing::PageContent content) {
    if (generation != generation_.value(page) || !requested_.remove(page)) {
        return;  // страницу изменили после запроса — придёт новый ответ
    }
    pages_.insert(page, std::move(content));
    matchActive(page);
    rebaseActive(page);
    emit contentLoaded(page);
    view_.viewport()->update();
}

// Блок активной правки в новых блоках страницы: те же операторы показа (после записи редактор указывает
// на новую модель).
void EditMode::matchActive(int page) {
    if (!active_ || active_->page != page) {
        return;
    }
    const auto it = pages_.constFind(page);
    if (it == pages_.constEnd()) {
        return;
    }
    active_->block = -1;
    const std::vector<int>& ops = active_->editor.source().ops;
    for (std::size_t i = 0; i < it->blocks.size(); ++i) {
        const editing::Block& b = it->blocks[i];
        if (b.editor && !ops.empty() && b.editor->source().ops == ops) {
            active_->block = static_cast<int>(i);
            break;
        }
    }
}

std::vector<EditMode::Frame> EditMode::frames() const {
    std::vector<Frame> out;
    for (const int page : view_.visiblePages()) {
        const auto it = pages_.constFind(page);
        if (it == pages_.constEnd()) {
            continue;
        }
        const QTransform t = view_.pageTransform(page);
        for (std::size_t i = 0; i < it->blocks.size(); ++i) {
            const editing::Block& b = it->blocks[i];
            if (active_ && active_->page == page && active_->block == static_cast<int>(i)) {
                continue;  // активный — своей рамкой ниже (вырос при наборе, сдвинут ручками)
            }
            Frame f;
            f.page = page;
            f.block = static_cast<int>(i);
            f.editable = b.editable();
            f.reason = b.reason;
            f.rect = t.mapRect(QRectF(QPointF(b.bbox.x0, b.bbox.y0), QPointF(b.bbox.x1, b.bbox.y1)));
            out.push_back(f);
        }
    }
    if (const std::optional<QRectF> r = activeFrame()) {
        Frame f;
        f.page = active_->page;
        f.block = active_->block;
        f.editable = true;
        f.active = true;
        f.rect = *r;
        out.push_back(f);
    }
    return out;
}

RectF EditMode::activeRect() const {
    if (!active_) {
        return RectF::none();
    }
    const edit::ParagraphEditor& e = active_->editor;
    RectF r = RectF::none();
    for (const QuadF& q : e.selectionQuads({0, e.size()})) {
        r = rectUnion(r, q.bounds());
    }
    if (active_->fresh || !r.valid()) {
        // Новый блок — во всю ширину набора и не ниже строки.
        const edit::ParagraphGeometry& g = e.geometry();
        const float size = active_->base ? active_->base->size : std::max(1.0f, g.fontSize);
        const float top = g.firstBaseline - size * 0.9f;
        for (const PointF p :
             {PointF{g.left, top}, PointF{g.left + g.width, top}, PointF{g.left, top + size * 1.2f},
              PointF{g.left + g.width, top + size * 1.2f}}) {
            const PointF q = mapPoint(p, g.toPage);
            r = rectUnion(r, RectF{q.x, q.y, q.x, q.y});
        }
    }
    return r.valid() ? r : active_->original;
}

std::optional<QRectF> EditMode::activeFrame() const {
    if (!active_ || view_.pageViewportRect(active_->page).isEmpty()) {
        return std::nullopt;
    }
    const RectF r = activeRect();
    return view_.pageTransform(active_->page).mapRect(QRectF(QPointF(r.x0, r.y0), QPointF(r.x1, r.y1)));
}

std::pair<PointF, PointF> EditMode::caretSegment(int pos) const {
    const edit::ParagraphEditor& e = active_->editor;
    if (!e.layout().empty()) {
        const edit::Caret c = e.caret(pos);
        return {c.top, c.bottom};
    }
    const edit::ParagraphGeometry& g = e.geometry();
    const float size = active_->base ? active_->base->size : std::max(1.0f, g.fontSize);
    return {mapPoint(PointF{g.left, g.firstBaseline - size * 0.9f}, g.toPage),
            mapPoint(PointF{g.left, g.firstBaseline + size * 0.25f}, g.toPage)};
}

PointF EditMode::pagePoint(int page, QPointF viewportPoint) const {
    const QPointF p = view_.viewportToPage(page, viewportPoint);
    return PointF{static_cast<float>(p.x()), static_cast<float>(p.y())};
}

std::pair<int, int> EditMode::blockAt(QPointF viewportPoint) const {
    const int page = view_.pageAtViewport(viewportPoint);
    if (page < 0) {
        return {-1, -1};
    }
    const auto it = pages_.constFind(page);
    if (it == pages_.constEnd()) {
        return {page, -1};
    }
    const PointF p = pagePoint(page, viewportPoint);
    const auto slack = static_cast<float>(2.0 / std::max(0.01f, view_.zoom()));
    // Активный блок (вырос при наборе) — первым.
    if (active_ && active_->page == page && active_->block >= 0) {
        for (const QuadF& q : active_->editor.selectionQuads({0, active_->editor.size()})) {
            if (blockContains(q.bounds(), p, slack)) {
                return {page, active_->block};
            }
        }
    }
    for (std::size_t i = 0; i < it->blocks.size(); ++i) {
        if (blockContains(it->blocks[i].bbox, p, slack)) {
            return {page, static_cast<int>(i)};
        }
    }
    return {page, -1};
}

QString EditMode::readOnlyTip(QPointF viewportPoint) const {
    const auto [page, block] = blockAt(viewportPoint);
    if (block < 0) {
        return {};
    }
    const editing::Block& b = pages_.value(page).blocks[static_cast<std::size_t>(block)];
    return b.editable() ? QString() : reasonText(b.reason);
}

// ---------------------------------------------------------------- мышь

bool EditMode::mousePress(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return false;
    }
    const QPointF pos = event->position();
    if (pressHandle(event)) {
        return true;
    }
    if (addText_ && !locked()) {
        const int target = view_.pageAtViewport(pos);
        if (target >= 0) {
            leaveBlock();
            addTextAt(target, view_.viewportToPage(target, pos));
        }
        return true;
    }
    const auto [page, block] = blockAt(pos);
    if (active_ && active_->block < 0 && active_->page == page) {
        // Новый блок (ещё не в блоках страницы): щелчок по нему — каретка.
        const RectF r = activeRect();
        const PointF p = pagePoint(page, pos);
        if (blockContains(r, p, 2.0f / std::max(0.01f, view_.zoom()))) {
            moveCaret(active_->editor.positionAt(p), (event->modifiers() & Qt::ShiftModifier) != 0);
            pressing_ = true;
            view_.setFocus();
            return true;
        }
    }
    if (page < 0 || block < 0) {
        leaveBlock();
        return true;
    }
    const editing::Block& b = pages_[page].blocks[static_cast<std::size_t>(block)];
    if (!b.editable() || locked()) {
        leaveBlock();
        return true;
    }
    // Двойной и тройной щелчок — слово и весь блок (Qt шлёт DoubleClick вместо второго Press).
    const bool again = lastClick_.isValid() && lastClick_.elapsed() < QApplication::doubleClickInterval() &&
                       (pos - lastClickPos_).manhattanLength() < QApplication::startDragDistance();
    clicks_ = again ? clicks_ + 1 : 1;
    lastClick_.start();
    lastClickPos_ = pos;
    const bool shift = (event->modifiers() & Qt::ShiftModifier) != 0;
    if (!active_ || active_->page != page || active_->block != block) {
        leaveBlock();
        if (!enterBlock(page, block, 0)) {
            return true;
        }
    }
    const int at = active_->editor.positionAt(pagePoint(page, pos));
    if (clicks_ >= 3) {
        selectAllInBlock();
    } else {
        moveCaret(at, shift);
        pressing_ = true;
    }
    view_.setFocus();
    return true;
}

bool EditMode::mouseMove(QMouseEvent* event) {
    const QPointF pos = event->position();
    if (drag_ != Handle::None && (event->buttons() & Qt::LeftButton) != 0) {
        return dragHandle(pos);
    }
    if (pressing_ && active_ && (event->buttons() & Qt::LeftButton) != 0) {
        moveCaret(active_->editor.positionAt(pagePoint(active_->page, pos)), true);
        return true;
    }
    const QString tip = readOnlyTip(pos);
    if (!tip.isEmpty()) {
        QToolTip::showText(view_.viewport()->mapToGlobal(pos.toPoint()), tip, view_.viewport());
    } else if (locked()) {
        const auto [page, block] = blockAt(pos);
        if (block >= 0) {
            QToolTip::showText(view_.viewport()->mapToGlobal(pos.toPoint()), locked_, view_.viewport());
        }
    } else {
        QToolTip::hideText();
    }
    return false;
}

bool EditMode::mouseRelease(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return false;
    }
    pressing_ = false;
    drag_ = Handle::None;
    return true;
}

bool EditMode::mouseDoubleClick(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return false;
    }
    const QPointF pos = event->position();
    const auto [page, block] = blockAt(pos);
    if (!active_ || active_->page != page || active_->block != block) {
        return mousePress(event);
    }
    clicks_ = 2;
    lastClick_.start();
    lastClickPos_ = pos;
    const edit::TextRange word = active_->editor.wordAt(active_->editor.positionAt(pagePoint(page, pos)));
    setSelection(word.begin, word.end);
    pressing_ = false;
    return true;
}

QCursor EditMode::cursor(QPointF point, Qt::KeyboardModifiers /*modifiers*/) const {
    switch (handleAt(point)) {
        case Handle::Move:
            return QCursor(Qt::SizeAllCursor);
        case Handle::Left:
        case Handle::Right:
            return QCursor(Qt::SizeHorCursor);
        case Handle::None:
            break;
    }
    if (addText_ && !locked() && view_.pageAtViewport(point) >= 0) {
        return QCursor(Qt::CrossCursor);
    }
    if (active_ && active_->block < 0 && activeFrame() && activeFrame()->contains(point)) {
        return QCursor(Qt::IBeamCursor);
    }
    const auto [page, block] = blockAt(point);
    if (block < 0) {
        return QCursor(Qt::ArrowCursor);
    }
    const editing::Block& b = pages_.value(page).blocks[static_cast<std::size_t>(block)];
    if (!b.editable() || locked()) {
        return QCursor(Qt::ForbiddenCursor);
    }
    return QCursor(Qt::IBeamCursor);
}

// ---------------------------------------------------------------- правка блока

bool EditMode::enterBlock(int page, int block, int pos) {
    if (locked()) {
        return false;
    }
    const auto it = pages_.constFind(page);
    if (it == pages_.constEnd() || block < 0 || block >= static_cast<int>(it->blocks.size())) {
        return false;
    }
    const editing::Block& b = it->blocks[static_cast<std::size_t>(block)];
    if (!b.editable()) {
        return false;
    }
    if (active_ && (active_->page != page || active_->block != block)) {
        leaveBlock();
    }
    if (!active_) {
        Active a;
        a.id = ++nextId_;
        a.page = page;
        a.block = block;
        a.original = b.bbox;
        a.editor = *b.editor;
        a.clean = *b.editor;
        active_ = std::move(a);
        controller_.inputMethodChanged();
        emit editingChanged(true);
    }
    const int at = pos < 0 ? active_->editor.size() : std::clamp(pos, 0, active_->editor.size());
    active_->anchor = at;
    active_->caret = at;
    caretMoved();
    return true;
}

edit::TextRange EditMode::selection() const noexcept {
    if (!active_) {
        return {};
    }
    return {std::min(active_->anchor, active_->caret), std::max(active_->anchor, active_->caret)};
}

void EditMode::setSelection(int anchor, int caret) {
    if (!active_) {
        return;
    }
    const int n = active_->editor.size();
    active_->anchor = std::clamp(anchor, 0, n);
    active_->caret = std::clamp(caret, 0, n);
    active_->typing.reset();
    caretMoved();
}

void EditMode::selectAllInBlock() {
    if (active_) {
        setSelection(0, active_->editor.size());
    }
}

void EditMode::insertText(const QString& text) {
    if (!active_ || text.isEmpty()) {
        return;
    }
    const std::u32string u = text.toStdU32String();
    edit::ParagraphEditor& e = active_->editor;
    const edit::TextRange sel = selection();
    int caret = 0;
    if (e.empty() && active_->base && !active_->typing) {
        caret = e.insert(0, u, *active_->base);
    } else if (active_->typing) {
        const int at = !sel.empty() ? e.erase(sel) : active_->caret;
        caret = e.insert(at, u, *active_->typing);
    } else if (!sel.empty()) {
        caret = e.replace(sel, u);
    } else {
        caret = e.insert(active_->caret, u);
    }
    active_->caret = caret;
    active_->anchor = caret;
    changed();
}

void EditMode::eraseSelectionOrChar(bool forward) {
    if (!active_) {
        return;
    }
    edit::TextRange r = selection();
    if (r.empty()) {
        const int c = active_->caret;
        r = forward ? edit::TextRange{c, c + 1} : edit::TextRange{c - 1, c};
        if (r.begin < 0 || r.end > active_->editor.size()) {
            return;
        }
    }
    const int caret = active_->editor.erase(r);
    active_->caret = caret;
    active_->anchor = caret;
    active_->typing.reset();
    changed();
}

void EditMode::applyStyle(const edit::StyleChange& change) {
    if (!active_) {
        return;
    }
    const edit::TextRange sel = selection();
    if (sel.empty()) {
        // Без выделения — стиль набора у каретки (как Ctrl+B в Acrobat перед набором).
        active_->typing =
            change.applied(active_->typing ? *active_->typing : active_->editor.typingStyle(active_->caret));
        emit caretChanged();
        return;
    }
    active_->editor.applyStyle(sel, change);
    changed();
}

std::optional<edit::CharStyle> EditMode::currentStyle() const {
    if (!active_) {
        return std::nullopt;
    }
    if (active_->typing) {
        return active_->typing;
    }
    const edit::TextRange sel = selection();
    if (!sel.empty()) {
        return active_->editor.styleAt(sel.begin);
    }
    return active_->editor.typingStyle(active_->caret);
}

void EditMode::toggleBold() {
    if (const auto s = currentStyle()) {
        edit::StyleChange c;
        c.bold = !s->bold;
        applyStyle(c);
    }
}

void EditMode::toggleItalic() {
    if (const auto s = currentStyle()) {
        edit::StyleChange c;
        c.italic = !s->italic;
        applyStyle(c);
    }
}

void EditMode::toggleUnderline() {
    if (const auto s = currentStyle()) {
        edit::StyleChange c;
        c.underline = !s->underline;
        applyStyle(c);
    }
}

bool EditMode::showsLocalCopy() const noexcept {
    return active_ && (active_->dirty || committing_ || renderWait_);
}

void EditMode::changed() {
    if (!active_) {
        return;
    }
    active_->dirty = true;
    ++active_->change;
    renderWait_ = false;
    commitTimer_.start();
    requestLayout();
    caretMoved();
}

void EditMode::requestLayout() {
    DocumentSession* session = view_.document();
    if (!active_ || session == nullptr) {
        return;
    }
    if (layoutBusy_) {
        layoutAgain_ = true;
        return;
    }
    layoutBusy_ = true;
    const quint64 change = active_->change;
    const quint64 epoch = epoch_;
    session->layoutText(active_->editor)
        .then(this, [this, change, epoch](QFuture<edit::ParagraphEditor> done) {
            if (epoch != epoch_) {
                return;
            }
            layoutBusy_ = false;
            try {
                edit::ParagraphEditor laid = done.result();
                if (active_ && active_->change == change) {
                    active_->editor.setLayout(laid.layout());
                    view_.viewport()->update();
                    emit caretChanged();
                }
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): курсор — по прежней раскладке
            }
            if (std::exchange(layoutAgain_, false) && active_ && active_->dirty) {
                requestLayout();
            }
        });
}

EditMode::Active* EditMode::copyById(quint64 id) {
    if (active_ && active_->id == id) {
        return &*active_;
    }
    for (Active& d : detached_) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

void EditMode::commit() {
    commitTimer_.stop();
    if (active_ && active_->dirty) {
        commitCopy(*active_);
    }
}

// Запись копии: по одной за раз (следующая ждёт новый источник абзаца — ответ предыдущей).
void EditMode::commitCopy(Active& copy) {
    DocumentSession* session = view_.document();
    if (!copy.dirty || session == nullptr) {
        return;
    }
    if (copy.fresh && copy.editor.empty()) {
        copy.dirty = false;  // новый блок без текста — писать нечего
        return;
    }
    if (committing_) {
        commitAgain_ = true;
        return;
    }
    committing_ = true;
    const quint64 id = copy.id;
    const quint64 change = copy.change;
    const quint64 epoch = epoch_;
    session->editText(copy.editor, QString::fromLatin1(editing::kEditTextStep))
        .then(this, [this, id, change, epoch](QFuture<EditTextResult> done) {
            if (epoch != epoch_) {
                return;
            }
            try {
                finishCommit(id, change, done.result());
            } catch (const std::exception& e) {
                failCommit(id, QCoreApplication::translate("ot::ui::EditMode", "Правка не записана: %1")
                                   .arg(QString::fromUtf8(e.what())));
            }
        });
}

void EditMode::finishCommit(quint64 id, quint64 change, const EditTextResult& result) {
    committing_ = false;
    const edit::RewriteReport& report = result.report;
    if (!report.ok()) {
        QString text;
        switch (report.error) {
            case edit::RewriteError::Signed:
                text = QCoreApplication::translate(
                    "ot::ui::EditMode",
                    "Документ подписан или ограничивает изменения: правка текста отключена");
                break;
            case edit::RewriteError::Stale:
                text = QCoreApplication::translate("ot::ui::EditMode",
                                                   "Абзац изменился в документе: правка не записана");
                break;
            case edit::RewriteError::ReadOnly:
                text = QCoreApplication::translate("ot::ui::EditMode", "Этот блок нельзя править");
                break;
            default:
                text = QCoreApplication::translate("ot::ui::EditMode", "Правка не записана");
                break;
        }
        failCommit(id, text);
        if (report.error == edit::RewriteError::Signed) {
            setLocked(text);
        }
        return;
    }
    if (Active* a = copyById(id)) {
        if (a->change == change) {
            // Пока писали, не набирали: копия — записанный абзац (новый источник и раскладка).
            a->editor = result.editor;
            a->dirty = false;
        } else {
            // Набрали ещё: источник — новый, текст — свой; запишется следующим разом.
            a->editor.markWritten(result.editor.source());
        }
        a->clean = result.editor;
        a->fresh = false;
        if (report.bbox.valid()) {
            a->original = rectUnion(a->original, report.bbox);
        }
        a->caret = std::clamp(a->caret, 0, a->editor.size());
        a->anchor = std::clamp(a->anchor, 0, a->editor.size());
        if (active_ && a == &*active_) {
            matchActive(a->page);
        } else if (!a->dirty) {
            afterglow_.push_back({a->page, a->original, a->editor});
            const auto it = std::find_if(detached_.begin(), detached_.end(),
                                         [id](const Active& d) { return d.id == id; });
            detached_.erase(it);
        }
    }
    startRenderWait();
    emit committed(report);
    // Очередь: сначала отложенные (вышли из блока), потом активная, если набрали во время записи.
    if (!detached_.empty()) {
        commitCopy(detached_.front());
    } else if (std::exchange(commitAgain_, false) && active_ && active_->dirty) {
        commitCopy(*active_);
    }
    view_.viewport()->update();
}

void EditMode::failCommit(quint64 id, const QString& message) {
    committing_ = false;
    commitAgain_ = false;
    // Копия бросается: документ как был, блоки страницы — заново.
    int page = -1;
    if (active_ && active_->id == id) {
        page = active_->page;
        dropActive();
    } else {
        const auto it =
            std::find_if(detached_.begin(), detached_.end(), [id](const Active& d) { return d.id == id; });
        if (it != detached_.end()) {
            page = it->page;
            detached_.erase(it);
        }
    }
    if (page >= 0) {
        pages_.remove(page);
        requested_.remove(page);
        generation_[page] = generation_.value(page) + 1;
        requestContent(page);
    }
    emit editFailed(message);
    if (!detached_.empty()) {
        commitCopy(detached_.front());
    }
    view_.viewport()->update();
}

// Блоки страницы пришли заново, а активная копия — по старой модели (записали соседний блок той же страницы):
// не правленая копия берёт редактор нового блока с тем же текстом.
void EditMode::rebaseActive(int page) {
    if (!active_ || active_->page != page || active_->block >= 0 || active_->dirty || committing_) {
        return;
    }
    const auto it = pages_.constFind(page);
    if (it == pages_.constEnd()) {
        return;
    }
    const std::u32string text = active_->editor.text();
    for (std::size_t i = 0; i < it->blocks.size(); ++i) {
        const editing::Block& b = it->blocks[i];
        if (b.editor && b.editor->text() == text && rectsMeet(b.bbox, active_->original)) {
            active_->editor = *b.editor;
            active_->clean = *b.editor;
            active_->block = static_cast<int>(i);
            active_->original = b.bbox;
            return;
        }
    }
}

void EditMode::startRenderWait() {
    renderWait_ = true;
    renderWaitPaints_ = 0;
    renderTimer_.start();
}

void EditMode::leaveBlock() {
    if (!active_) {
        return;
    }
    commitTimer_.stop();
    if (active_->fresh && active_->editor.empty()) {
        dropActive();  // пустой новый блок исчезает
        return;
    }
    if (active_->dirty || (committing_ && copyById(active_->id) != nullptr)) {
        Active copy = std::move(*active_);
        copy.typing.reset();
        copy.preedit.clear();
        active_.reset();
        detached_.push_back(std::move(copy));
        if (!committing_) {
            commitCopy(detached_.back());
        }
    }
    dropActive();
}

void EditMode::setAlignment(edit::Align align) {
    if (!active_ || active_->editor.alignment() == align) {
        return;
    }
    active_->editor.setAlignment(align);
    changed();
}

void EditMode::setLineSpacing(float factor) {
    if (!active_ || factor <= 0 || std::abs(active_->editor.lineSpacing() - factor) < 1e-4f) {
        return;
    }
    active_->editor.setLineSpacing(factor);
    changed();
}

void EditMode::setScript(edit::Script script) {
    if (const auto st = currentStyle()) {
        if (edit::scriptOf(*st) != script) {
            applyStyle(edit::scriptChange(*st, script));
        }
    }
}

bool EditMode::revertPending() {
    if (!active_ || !active_->dirty || committing_) {
        return false;
    }
    commitTimer_.stop();
    active_->editor = active_->clean;
    active_->dirty = false;
    ++active_->change;
    active_->caret = std::clamp(active_->caret, 0, active_->editor.size());
    active_->anchor = active_->caret;
    active_->typing.reset();
    caretMoved();
    return true;
}

void EditMode::dropActive() {
    const bool was = active_.has_value();
    active_.reset();
    blink_.stop();
    commitTimer_.stop();
    controller_.inputMethodChanged();
    if (was) {
        emit editingChanged(false);
        emit caretChanged();
    }
    view_.viewport()->update();
}

}  // namespace ot::ui
