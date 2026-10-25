// Формы через DocumentSession (план 6.1; core/forms/fields.h): чтение полей — задачей потока документа,
// правка — edit с шагом истории («Fill field», «Reset form», «Click button») и pageChanged по страницам, вид
// которых изменился. ui зовёт эти функции, а не TaskContext.
#pragma once

#include <QFuture>
#include <QString>
#include <QStringList>

#include <map>
#include <vector>

#include "core/forms/fields.h"
#include "services/document_session.h"

namespace ot::forms {

inline constexpr const char* kFillStep = "Fill field";
inline constexpr const char* kResetStep = "Reset form";
inline constexpr const char* kClickStep = "Click button";
inline constexpr const char* kCalculateStep = "Calculate form";

QFuture<Form> read(DocumentSession& session, TaskPriority priority = TaskPriority::High);
// Поля и порядок табуляции страниц с полями — одной задачей (слой заполнения ui).
struct FormSnapshot {
    Form form;
    std::map<int, std::vector<int>> tabs;  // страница → виджеты в порядке Tab
};
QFuture<FormSnapshot> snapshot(DocumentSession& session);
QFuture<std::vector<PageWidget>> widgets(DocumentSession& session, int page,
                                         TaskPriority priority = TaskPriority::High);
QFuture<std::vector<int>> tabSequence(DocumentSession& session, int page);
// Значение для показа (формат при включённом JavaScript).
QFuture<QString> formatted(DocumentSession& session, QString name);

QFuture<SetResult> setValue(DocumentSession& session, QString name, QString value, bool recalculate = true);
QFuture<SetResult> setValues(DocumentSession& session, QString name, QStringList values,
                             bool recalculate = true);
QFuture<SetResult> toggle(DocumentSession& session, int page, int objnum, bool recalculate = true);
// «Очистить форму» (forms-06): names пусто — все.
QFuture<std::vector<int>> reset(DocumentSession& session, QStringList names = {}, bool exclude = false);
QFuture<ClickResult> click(DocumentSession& session, int page, int objnum);
QFuture<std::vector<int>> focus(DocumentSession& session, int page, int objnum, bool in);
QFuture<std::vector<int>> recalculate(DocumentSession& session);

}  // namespace ot::forms
