// Пункты комментариев в контекстном меню «Выбора объектов» (план 5.1, 5.2; эталон 3.4 «Выделение и правка на
// странице»): «Открыть окно записки» (у поля FreeText — «Изменить текст»), «Свойства…» (AnnotPropsDialog,
// comment-08), «Сделать свойствами по умолчанию» (цвет, заливка, непрозрачность, толщина, значок, концы линии
// и шрифт выбранного — стиль его инструмента, commenting/toolStyles); 5.4 — «Ответить» (поле ответа панели
// «Комментарии»), «Статус ▸» и «Отметить галочкой» (ui/panes/comment_actions.h). Двойной щелчок по
// выбранному — правка поля или окно записки. Ставит DocumentView на свой вид.
#pragma once

#include <QString>

#include "core/annot/annots.h"

namespace ot::ui {

class PageView;

void installCommentMenu(PageView& view);

// Ключ стиля инструмента (commenting/toolStyles) по свойствам аннотации: "note", "textBox", "callout",
// "line", "arrow", "rectangle", "oval", "polygon", "cloud", "polyline", "stamp"…
QString styleKeyFor(const annot::AnnotProps& props);
// «Сделать свойствами по умолчанию»: стиль инструмента этого типа — из свойств props.
void saveAsDefault(const annot::AnnotProps& props);

}  // namespace ot::ui
