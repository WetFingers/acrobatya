// Каретка и клавиши режима «Редактировать» (план 3.4, п. 1): набор (keyPressEvent и IME), Backspace/Del,
// стрелки (Ctrl — по словам), Shift — выделение, Home/End (Ctrl — начало и конец блока), Enter — перевод
// строки в абзаце, Ctrl+A — весь блок, Ctrl+B/I/U — начертание, Ctrl+C/X/V — простой текст, Ctrl+Z до записи
// — бросить незаписанную правку блока. Курсор метода ввода (ImCursorRectangle) — у каретки.
#include <QClipboard>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMimeData>

#include <algorithm>

#include "ui/tools/edit/edit_mode.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

namespace {

bool isWordChar(char32_t c) { return QChar::isLetterOrNumber(c) || QChar::isMark(c) || c == U'_'; }

// Текст клавиши, который набирается (без управляющих символов и ярлыков с Ctrl/Alt; AltGr = Ctrl+Alt —
// набирает).
QString typedText(const QKeyEvent* event) {
    const Qt::KeyboardModifiers mods = event->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
    const bool altGr = (mods & Qt::ControlModifier) != 0 && (mods & Qt::AltModifier) != 0;
    if (mods != Qt::NoModifier && !altGr) {
        return {};
    }
    QString text = event->text();
    text.removeIf([](QChar c) { return c.category() == QChar::Other_Control; });
    return text;
}

}  // namespace

bool EditMode::wantsShortcut(const QKeyEvent* event) const {
    if (!active_) {
        return false;
    }
    if (!typedText(event).isEmpty()) {
        return true;  // буква — не однобуквенный ярлык окна
    }
    const Qt::KeyboardModifiers mods = event->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
    const int key = event->key();
    switch (key) {
        case Qt::Key_Backspace:
        case Qt::Key_Delete:
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Home:
        case Qt::Key_End:
        case Qt::Key_Return:
        case Qt::Key_Enter:
            return mods == Qt::NoModifier || mods == Qt::ControlModifier;
        default:
            break;
    }
    if (mods == Qt::ControlModifier) {
        switch (key) {
            case Qt::Key_A:
            case Qt::Key_B:
            case Qt::Key_I:
            case Qt::Key_U:
            case Qt::Key_C:
            case Qt::Key_X:
            case Qt::Key_V:
                return true;
            case Qt::Key_Z:
                return active_->dirty && !committing_;  // после записи — отмена шага истории окна
            default:
                break;
        }
    }
    return false;
}

bool EditMode::keyPress(QKeyEvent* event) {
    if (!active_) {
        return false;
    }
    return editKey(event);
}

bool EditMode::editKey(QKeyEvent* event) {
    const Qt::KeyboardModifiers all = event->modifiers() & ~Qt::KeypadModifier;
    const bool shift = (all & Qt::ShiftModifier) != 0;
    const Qt::KeyboardModifiers mods = all & ~Qt::ShiftModifier;
    const bool ctrl = mods == Qt::ControlModifier;
    const int key = event->key();
    const int caret = active_->caret;
    edit::ParagraphEditor& e = active_->editor;

    if (key == Qt::Key_Escape && mods == Qt::NoModifier) {
        leaveBlock();
        return true;
    }
    if (ctrl) {
        switch (key) {
            case Qt::Key_A:
                selectAllInBlock();
                return true;
            case Qt::Key_B:
                toggleBold();
                return true;
            case Qt::Key_I:
                toggleItalic();
                return true;
            case Qt::Key_U:
                toggleUnderline();
                return true;
            case Qt::Key_C:
                copySelection(false);
                return true;
            case Qt::Key_X:
                copySelection(true);
                return true;
            case Qt::Key_V:
                paste();
                return true;
            case Qt::Key_Z:
                return revertPending();
            default:
                break;
        }
    }
    if (mods == Qt::NoModifier || ctrl) {
        switch (key) {
            case Qt::Key_Backspace:
                if (ctrl && selection().empty()) {
                    setSelection(wordStep(caret, -1), caret);
                }
                eraseSelectionOrChar(false);
                return true;
            case Qt::Key_Delete:
                if (ctrl && selection().empty()) {
                    setSelection(caret, wordStep(caret, 1));
                }
                eraseSelectionOrChar(true);
                return true;
            case Qt::Key_Left:
                if (!shift && !selection().empty()) {
                    moveCaret(selection().begin, false);
                } else {
                    moveCaret(ctrl ? wordStep(caret, -1) : caret - 1, shift);
                }
                return true;
            case Qt::Key_Right:
                if (!shift && !selection().empty()) {
                    moveCaret(selection().end, false);
                } else {
                    moveCaret(ctrl ? wordStep(caret, 1) : caret + 1, shift);
                }
                return true;
            case Qt::Key_Up:
                moveCaret(e.moveLine(caret, -1), shift);
                return true;
            case Qt::Key_Down:
                moveCaret(e.moveLine(caret, 1), shift);
                return true;
            case Qt::Key_Home:
                moveCaret(ctrl ? 0 : lineEdge(caret, false), shift);
                return true;
            case Qt::Key_End:
                moveCaret(ctrl ? e.size() : lineEdge(caret, true), shift);
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                if (!ctrl) {
                    insertText(QStringLiteral("\n"));
                    return true;
                }
                break;
            default:
                break;
        }
    }
    const QString text = typedText(event);
    if (!text.isEmpty()) {
        insertText(text);
        return true;
    }
    return false;
}

void EditMode::moveCaret(int pos, bool extend) {
    if (!active_) {
        return;
    }
    const int p = std::clamp(pos, 0, active_->editor.size());
    active_->caret = p;
    if (!extend) {
        active_->anchor = p;
    }
    active_->typing.reset();
    caretMoved();
}

int EditMode::lineEdge(int pos, bool end) const {
    const edit::ParagraphEditor& e = active_->editor;
    const auto& lines = e.layout().lines;
    if (lines.empty()) {
        return end ? e.size() : 0;
    }
    const auto& line = lines[static_cast<std::size_t>(std::clamp(e.lineOf(pos), 0, e.lineCount() - 1))];
    if (!end) {
        return line.begin;
    }
    int at = line.end;
    // Конец перенесённой строки — перед пробелом переноса.
    if (&line != &lines.back() && at > line.begin) {
        const char32_t c = e.chars()[static_cast<std::size_t>(at - 1)].ch;
        if (c == U' ' || c == U'\n') {
            --at;
        }
    }
    return at;
}

int EditMode::wordStep(int pos, int delta) const {
    const auto& chars = active_->editor.chars();
    const int n = static_cast<int>(chars.size());
    auto word = [&](int i) { return i >= 0 && i < n && isWordChar(chars[static_cast<std::size_t>(i)].ch); };
    int p = pos;
    if (delta < 0) {
        while (p > 0 && !word(p - 1)) {
            --p;
        }
        while (p > 0 && word(p - 1)) {
            --p;
        }
    } else {
        while (p < n && word(p)) {
            ++p;
        }
        while (p < n && !word(p)) {
            ++p;
        }
    }
    return p;
}

void EditMode::copySelection(bool cut) {
    const edit::TextRange sel = selection();
    if (sel.empty()) {
        return;
    }
    const std::u32string text = active_->editor.text(sel);
    QString plain = QString::fromStdU32String(text);
    plain.replace(QChar(u'\n'), QStringLiteral("\r\n"));
    if (QClipboard* clip = QGuiApplication::clipboard()) {
        clip->setText(plain);
    }
    if (cut) {
        eraseSelectionOrChar(false);
    }
}

void EditMode::paste() {
    QClipboard* clip = QGuiApplication::clipboard();
    if (clip == nullptr) {
        return;
    }
    QString text = clip->text();
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    insertText(text);
}

void EditMode::caretMoved() {
    restartBlink();
    if (view_.hasFocus()) {
        QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle | Qt::ImCursorPosition |
                                               Qt::ImAnchorPosition | Qt::ImSurroundingText);
    }
    view_.viewport()->update();
    emit caretChanged();
}

void EditMode::restartBlink() {
    caretOn_ = true;
    if (active_) {
        blink_.start();
    } else {
        blink_.stop();
    }
}

// ---------------------------------------------------------------- метод ввода (IME)

bool EditMode::inputMethodEvent(QInputMethodEvent* event) {
    if (!active_) {
        return false;
    }
    const QString commit = event->commitString();
    if (event->replacementLength() > 0 && selection().empty()) {
        const int from = std::clamp(active_->caret + event->replacementStart(), 0, active_->editor.size());
        setSelection(from, std::min(active_->editor.size(), from + event->replacementLength()));
    }
    active_->preedit = event->preeditString();
    if (!commit.isEmpty()) {
        insertText(commit);
    } else {
        view_.viewport()->update();
    }
    return true;
}

QVariant EditMode::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (!active_) {
        return {};
    }
    switch (query) {
        case Qt::ImEnabled:
            return true;
        case Qt::ImHints:
            return static_cast<int>(Qt::ImhMultiLine);
        case Qt::ImCursorRectangle:
        case Qt::ImAnchorRectangle: {
            const int at = query == Qt::ImCursorRectangle ? active_->caret : active_->anchor;
            const auto [ct, cb] = caretSegment(at);
            const QTransform t = view_.pageTransform(active_->page);
            const QPointF top = t.map(QPointF(ct.x, ct.y));
            const QPointF bottom = t.map(QPointF(cb.x, cb.y));
            return QRectF(top, bottom).normalized().adjusted(0, 0, 1, 0);
        }
        case Qt::ImCursorPosition:
            return active_->caret;
        case Qt::ImAnchorPosition:
            return active_->anchor;
        case Qt::ImSurroundingText:
            return QString::fromStdU32String(active_->editor.text());
        case Qt::ImCurrentSelection:
            return QString::fromStdU32String(active_->editor.text(selection()));
        default:
            return {};
    }
}

}  // namespace ot::ui
