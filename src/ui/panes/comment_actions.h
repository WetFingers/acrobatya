// Общее для панели «Комментарии», окна записки, меню «Выбора объектов» и свойств (план 5.4; эталон 3.4
// «Попапы и ответы»; comment-20, comment-21): подписи типов и статусов, дата карточки, HTML ветки ответов
// (/RC — простой HTML: жирный, курсив, подчёркивание, цвет) и действия статуса — «Статус ▸» (Нет, Принято,
// Отклонено, Отменено, Завершено — /StateModel Review) и «Отметить галочкой» (/StateModel Marked); автор
// статуса — Identity (Установки → Комментирование). Статус — отдельная скрытая аннотация (core/annot/
// comments.h setStatus), шаги «Set status» / «Set check mark».
#pragma once

#include <QString>

#include <cstdint>
#include <string>
#include <vector>

#include "core/annot/comments.h"

class QAction;
class QMenu;

namespace ot {
class DocumentSession;
}

namespace ot::ui::comment_actions {

// «Записка», «Выделение текста»… (ответ — «Ответ»).
QString typeTitle(const annot::Comment& c);
// Иконка Lucide типа (ответ — reply).
QString typeIcon(const annot::Comment& c);
// «Нет», «Принято», «Отклонено», «Отменено», «Завершено».
QString stateTitle(annot::ReviewState state);
// dd.MM.yyyy HH:mm; 0 — пусто.
QString dateText(std::int64_t seconds);
// /RC (или текст, если /RC нет) — простым HTML для QLabel/QTextEdit.
QString richHtml(const std::string& richContents, const std::string& plain);
// Ответы ветки и статус — HTML (окно записки, подсказки).
QString threadHtml(const annot::Comment& c);

// Подменю «Статус» записи ref: текущий отмечен; выбор — статус автора Identity. enabled = false — серое.
QMenu* addStatusMenu(QMenu& menu, DocumentSession& session, const annot::AnnotRef& ref,
                     annot::ReviewState current, bool enabled);
// «Отметить галочкой» (переключатель).
QAction* addCheckAction(QMenu& menu, DocumentSession& session, const annot::AnnotRef& ref, bool marked,
                        bool enabled);

}  // namespace ot::ui::comment_actions
