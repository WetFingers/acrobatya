#include "ui/tools/comment/reply_editor.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFragment>
#include <QVBoxLayout>

#include <algorithm>

#include "ui/theme/theme.h"
#include "ui/theme/tokens.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

namespace {

enum Format { Bold, Italic, Underline, Ink };

void pushSpan(std::vector<annot::RichSpan>& out, annot::RichSpan span) {
    if (span.text.empty()) {
        return;
    }
    if (!out.empty()) {
        annot::RichSpan& last = out.back();
        if (last.bold == span.bold && last.italic == span.italic && last.underline == span.underline &&
            last.color == span.color) {
            last.text += span.text;
            return;
        }
    }
    out.push_back(std::move(span));
}

}  // namespace

ReplyEditor::ReplyEditor(QWidget* parent) : QWidget(parent), ink_(kAnnotationColors[6].color()) {
    setObjectName(QStringLiteral("replyEditor"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 2, m.grid * 2, m.grid * 2, m.grid * 2);
    column->setSpacing(m.grid);
    title_ = new QLabel(this);
    title_->setObjectName(QStringLiteral("replyEditorTitle"));
    title_->setWordWrap(true);
    column->addWidget(title_);
    editor_ = new QTextEdit(this);
    editor_->setObjectName(QStringLiteral("replyEditorText"));
    editor_->setAcceptRichText(false);  // вставка — простым текстом; оформление — кнопками
    editor_->setTabChangesFocus(true);
    editor_->setMinimumHeight(m.cardRow);
    editor_->setMaximumHeight(m.cardRow * 2);
    editor_->installEventFilter(this);
    column->addWidget(editor_);
    auto* row = new QHBoxLayout();
    row->setSpacing(m.grid);
    const auto button = [this, row](const char* icon, const QString& label, int which) {
        auto* b = new ToolButton(QLatin1String(icon), label, this);
        b->setIconSizeKind(ToolButton::IconSize::Row);
        b->setCheckable(true);
        connect(b, &ToolButton::clicked, this, [this, which] { toggleFormat(which); });
        row->addWidget(b);
        return b;
    };
    bold_ = button("bold", tr("Жирный (Ctrl+B)"), Bold);
    bold_->setObjectName(QStringLiteral("replyBold"));
    italic_ = button("italic", tr("Курсив (Ctrl+I)"), Italic);
    italic_->setObjectName(QStringLiteral("replyItalic"));
    underline_ = button("underline", tr("Подчёркнутый (Ctrl+U)"), Underline);
    underline_->setObjectName(QStringLiteral("replyUnderline"));
    color_ = button("palette", tr("Цвет текста"), Ink);
    color_->setObjectName(QStringLiteral("replyColor"));
    row->addStretch(1);
    column->addLayout(row);
    // Кнопки — своей строкой: в панели 280 px рядом с оформлением «Ответить» не помещалась.
    auto* actions = new QHBoxLayout();
    actions->setSpacing(m.grid * 2);
    actions->addStretch(1);
    cancel_ = new QPushButton(tr("Отмена"), this);
    cancel_->setObjectName(QStringLiteral("replyCancel"));
    submit_ = new QPushButton(tr("Ответить"), this);
    submit_->setObjectName(QStringLiteral("replySubmit"));
    submit_->setDefault(true);
    submit_->setProperty("primary", true);
    actions->addWidget(cancel_);
    actions->addWidget(submit_);
    column->addLayout(actions);
    connect(submit_, &QPushButton::clicked, this, [this] {
        if (!isEmpty()) {
            emit submitted();
        }
    });
    connect(cancel_, &QPushButton::clicked, this, &ReplyEditor::cancelled);
    connect(editor_, &QTextEdit::currentCharFormatChanged, this, &ReplyEditor::syncButtons);
    connect(editor_, &QTextEdit::textChanged, this, [this] { submit_->setEnabled(!isEmpty()); });
    editor_->setAccessibleName(tr("Текст ответа"));
    submit_->setEnabled(false);
}

void ReplyEditor::setTitle(const QString& title, const QString& submitText) {
    title_->setText(title);
    submit_->setText(submitText);
}

void ReplyEditor::setInkColor(const QColor& color) { ink_ = color; }

void ReplyEditor::toggleFormat(int which) {
    QTextCharFormat current = editor_->currentCharFormat();
    QTextCharFormat f;
    switch (which) {
        case Bold:
            f.setFontWeight(current.fontWeight() >= QFont::DemiBold ? QFont::Normal : QFont::Bold);
            break;
        case Italic:
            f.setFontItalic(!current.fontItalic());
            break;
        case Underline:
            f.setFontUnderline(!current.fontUnderline());
            break;
        case Ink:
            if (current.hasProperty(QTextFormat::ForegroundBrush)) {
                QTextCursor c = editor_->textCursor();
                QTextCharFormat clear = current;
                clear.clearForeground();
                if (c.hasSelection()) {
                    c.setCharFormat(clear);
                }
                editor_->setCurrentCharFormat(clear);
                syncButtons();
                editor_->setFocus();
                return;
            }
            f.setForeground(ink_);
            break;
        default:
            return;
    }
    editor_->mergeCurrentCharFormat(f);
    syncButtons();
    editor_->setFocus();
}

void ReplyEditor::syncButtons() {
    const QTextCharFormat f = editor_->currentCharFormat();
    bold_->setChecked(f.fontWeight() >= QFont::DemiBold);
    italic_->setChecked(f.fontItalic());
    underline_->setChecked(f.fontUnderline());
    color_->setChecked(f.hasProperty(QTextFormat::ForegroundBrush));
}

std::vector<annot::RichSpan> ReplyEditor::spansOf(const QTextDocument& doc) {
    std::vector<annot::RichSpan> out;
    bool first = true;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        if (!first) {
            pushSpan(out, {.text = "\n"});
        }
        first = false;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid()) {
                continue;
            }
            const QTextCharFormat f = frag.charFormat();
            annot::RichSpan s;
            s.text = frag.text().toStdString();
            s.bold = f.fontWeight() >= QFont::DemiBold;
            s.italic = f.fontItalic();
            s.underline = f.fontUnderline();
            if (f.hasProperty(QTextFormat::ForegroundBrush)) {
                s.color = static_cast<std::uint32_t>(f.foreground().color().rgb() & 0xFFFFFFu);
            }
            pushSpan(out, std::move(s));
        }
    }
    // Хвостовые пустые абзацы не нужны.
    while (!out.empty() && !out.back().text.empty() && out.back().text.back() == '\n') {
        out.back().text.pop_back();
        if (out.back().text.empty()) {
            out.pop_back();
        }
    }
    return out;
}

void ReplyEditor::fill(QTextDocument& doc, const std::vector<annot::RichSpan>& spans) {
    doc.clear();
    QTextCursor c(&doc);
    for (const annot::RichSpan& s : spans) {
        QTextCharFormat f;
        f.setFontWeight(s.bold ? QFont::Bold : QFont::Normal);
        f.setFontItalic(s.italic);
        f.setFontUnderline(s.underline);
        if (s.color != annot::kNoRichColor) {
            f.setForeground(QColor::fromRgb(s.color));
        }
        const QStringList pieces = QString::fromStdString(s.text).split(QLatin1Char('\n'));
        for (int i = 0; i < pieces.size(); ++i) {
            if (i > 0) {
                c.insertBlock();
            }
            c.insertText(pieces[i], f);
        }
    }
}

void ReplyEditor::setSpans(const std::vector<annot::RichSpan>& spans) {
    fill(*editor_->document(), spans);
    editor_->moveCursor(QTextCursor::End);
    syncButtons();
}

std::vector<annot::RichSpan> ReplyEditor::spans() const { return spansOf(*editor_->document()); }

std::string ReplyEditor::plainText() const { return annot::plainText(spans()); }

std::string ReplyEditor::richContents() const {
    const std::vector<annot::RichSpan> s = spans();
    const bool styled = std::any_of(s.begin(), s.end(), [](const annot::RichSpan& r) {
        return r.bold || r.italic || r.underline || r.color != annot::kNoRichColor;
    });
    return styled ? annot::richContentsFrom(s) : std::string();
}

bool ReplyEditor::isEmpty() const { return editor_->toPlainText().trimmed().isEmpty(); }

void ReplyEditor::clear() {
    editor_->clear();
    editor_->setCurrentCharFormat({});
    syncButtons();
}

bool ReplyEditor::eventFilter(QObject* watched, QEvent* event) {
    if (watched == editor_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        const bool ctrl = (key->modifiers() & Qt::ControlModifier) != 0;
        if (ctrl && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
            if (!isEmpty()) {
                emit submitted();
            }
            return true;
        }
        if (key->key() == Qt::Key_Escape) {
            emit cancelled();
            return true;
        }
        if (ctrl && key->key() == Qt::Key_B) {
            toggleFormat(Bold);
            return true;
        }
        if (ctrl && key->key() == Qt::Key_I) {
            toggleFormat(Italic);
            return true;
        }
        if (ctrl && key->key() == Qt::Key_U) {
            toggleFormat(Underline);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace ot::ui
