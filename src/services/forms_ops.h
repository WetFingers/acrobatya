// Формы через DocumentSession (план 6.1; core/forms/fields.h): чтение полей — задачей потока документа,
// правка — edit с шагом истории («Fill field», «Reset form», «Click button») и pageChanged по страницам, вид
// которых изменился. ui зовёт эти функции, а не TaskContext.
#pragma once

#include <QByteArray>
#include <QFuture>
#include <QString>
#include <QStringList>

#include <map>
#include <vector>

#include "core/forms/data_io.h"
#include "core/forms/fields.h"
#include "core/forms/submit.h"
#include "core/util/http_fetcher.h"
#include "services/document_session.h"
#include "services/mail.h"

namespace ot::forms {

inline constexpr const char* kFillStep = "Fill field";
inline constexpr const char* kResetStep = "Reset form";
inline constexpr const char* kClickStep = "Click button";
inline constexpr const char* kCalculateStep = "Calculate form";
inline constexpr const char* kImportDataStep = "Import form data";

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

// Данные формы (forms-35, forms-36; core/forms/data_io.h): байты файла формата format (fileName — /F и href).
QFuture<QByteArray> exportData(DocumentSession& session, DataFormat format, QString fileName);
// Импорт разобранных данных шагом «Import form data»; documentFile — имя файла документа для проверки /F
// (чужой — DataError в QFuture).
QFuture<DataImportReport> importData(DocumentSession& session, FormData data, QString documentFile);

// Отправка формы (forms-30, cloud-08; core/forms/submit.h): план действия /SubmitForm (имя файла документа — /F и
// имя вложения), тело (PDF — копия документа с правками: Saver::saveCopy во временный файл, история цела),
// письмо (mailto: — вложение во временном файле, Mailer) и запрос (http(s) — HttpFetcher, Purpose::General).
// Подтверждение человека — до этих вызовов (ui).
QFuture<SubmitPlan> planSubmit(DocumentSession& session, FieldAction action);
QFuture<QByteArray> submitBody(DocumentSession& session, SubmitPlan plan);
// pdf — тело формата PDF (submitBody); у прочих форматов — plan.body.
MailResult sendMail(const SubmitPlan& plan, const QByteArray& pdf, Mailer& mailer);
// Синхронно (из рабочего потока, не интерфейса).
net::Response sendHttp(const SubmitPlan& plan, const QByteArray& pdf, net::HttpFetcher& fetcher);

}  // namespace ot::forms
