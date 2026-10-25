// Поле ответа и правки текста комментария (план 5.4; эталон 3.4 «Попапы и ответы»; comment-20): текст с
// простым оформлением — жирный, курсив, подчёркивание, цвет (кнопки и Ctrl+B/I/U) — в /RC (core/annot/
// comments.h richContentsFrom) и простым текстом в /Contents; «@имя» — просто текст. Ctrl+Enter или
// «Ответить» — submitted, Esc или «Отмена» — cancelled. Панель «Комментарии» держит его под списком.
#pragma once

#include <QWidget>

#include <vector>

#include "core/annot/comments.h"

class QLabel;
class QPushButton;
class QTextDocument;
class QTextEdit;

namespace ot::ui {

class ToolButton;

class ReplyEditor : public QWidget {
    Q_OBJECT
public:
    explicit ReplyEditor(QWidget* parent = nullptr);

    QTextEdit* editor() const { return editor_; }
    QLabel* titleLabel() const { return title_; }
    ToolButton* boldButton() const { return bold_; }
    ToolButton* italicButton() const { return italic_; }
    ToolButton* underlineButton() const { return underline_; }
    ToolButton* colorButton() const { return color_; }
    QPushButton* submitButton() const { return submit_; }
    QPushButton* cancelButton() const { return cancel_; }

    // Заголовок («Ответ: Борис Петров», «Изменить комментарий») и подпись кнопки.
    void setTitle(const QString& title, const QString& submitText);
    void setSpans(const std::vector<annot::RichSpan>& spans);
    std::vector<annot::RichSpan> spans() const;
    // Простой текст (/Contents) и /RC (пусто — оформления нет: /RC не нужен).
    std::string plainText() const;
    std::string richContents() const;
    bool isEmpty() const;
    void clear();
    // Цвет кнопки «Цвет» (по умолчанию — синий первой палитры комментариев).
    void setInkColor(const QColor& color);

    // QTextDocument ↔ отрезки /RC (абзацы — '\n').
    static std::vector<annot::RichSpan> spansOf(const QTextDocument& doc);
    static void fill(QTextDocument& doc, const std::vector<annot::RichSpan>& spans);

signals:
    void submitted();
    void cancelled();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void toggleFormat(int which);
    void syncButtons();

    QLabel* title_ = nullptr;
    QTextEdit* editor_ = nullptr;
    ToolButton* bold_ = nullptr;
    ToolButton* italic_ = nullptr;
    ToolButton* underline_ = nullptr;
    ToolButton* color_ = nullptr;
    QPushButton* submit_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QColor ink_;
};

}  // namespace ot::ui
