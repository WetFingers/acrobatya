// Быстрые действия над выделенным текстом (план 5.1; эталон 3.4, 6.1; comment-10, comment-02): после
// выделения текста «Выделением» над выделением — плавающая панель «Выделить», «Подчеркнуть», «Зачеркнуть»,
// «Добавить комментарий» (выделение и окно записки для набора), «Копировать». Разметка — по аннотации на
// страницу выделения (CommentMode::markupSelection, стиль инструмента «Добавить комментарии»), после неё
// выделение снимается. Правка комментариев запрещена — кнопки разметки серые (копировать можно, если право
// /P разрешает копирование). Панель живёт на вьюпорте вида (её создаёт DocumentView), видна, пока у
// «Выделения» есть выделение и кнопка мыши отпущена; следует за прокруткой и масштабом.
#pragma once

#include <QPointer>

#include "ui/widgets/floating_bar.h"

namespace ot::ui {

class PageView;

class QuickActionsBar : public FloatingBar {
    Q_OBJECT
public:
    explicit QuickActionsBar(PageView& view);

    // Показать/спрятать и поставить над выделением.
    void sync();
    // Действия по id кнопки: highlight, underline, strikeout, note, copy.
    void run(const QString& id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    PageView& view_;
};

}  // namespace ot::ui
