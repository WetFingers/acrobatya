// Отправка формы (план 6.1, п. 4; эталон 3.6 «Данные», 7.4; forms-30): разбор действия /SubmitForm — адрес /F,
// флаги /Flags (ISO 32000-1, табл. 237), поля /Fields с «включить/исключить», формат — и тело запроса:
//   FDF (по умолчанию)           — application/vnd.fdf, файл core/forms/data_io.h с /F документа;
//   XFDF (флаг XFDF)             — application/xml (как у Acrobat — XML XFDF);
//   HTML (флаг ExportFormat)     — application/x-www-form-urlencoded «имя=значение&…» (GET — в адресе);
//   PDF (флаг SubmitPDF)         — application/pdf, документ целиком (тело собирает служба: запись копии).
// Адрес mailto: — письмо с вложением (получатели, тема и текст — из адреса; services/mail.h), http(s) — запрос
// через HttpFetcher после подтверждения человеком (эталон 7.4: без подтверждения ничего не уходит); прочее —
// не поддерживается. Ответ сервера с FDF («%FDF» или application/vnd.fdf) импортируется в форму.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/forms/data_io.h"
#include "core/forms/fields.h"

namespace ot {
class Document;
}

namespace ot::forms {

namespace submit_flags {
inline constexpr int kExclude = 1;
inline constexpr int kIncludeNoValueFields = 1 << 1;
inline constexpr int kExportFormat = 1 << 2;  // HTML
inline constexpr int kGetMethod = 1 << 3;
inline constexpr int kSubmitCoordinates = 1 << 4;
inline constexpr int kXfdf = 1 << 5;
inline constexpr int kIncludeAppendSaves = 1 << 6;
inline constexpr int kIncludeAnnotations = 1 << 7;
inline constexpr int kSubmitPdf = 1 << 8;
inline constexpr int kCanonicalFormat = 1 << 9;
inline constexpr int kExclNonUserAnnots = 1 << 10;
inline constexpr int kExclFKey = 1 << 11;
inline constexpr int kEmbedForm = 1 << 13;
}  // namespace submit_flags

enum class SubmitFormat { Fdf, Xfdf, Html, Pdf };
const char* submitFormatName(SubmitFormat format) noexcept;   // "FDF", "XFDF", "HTML", "PDF"
const char* submitContentType(SubmitFormat format) noexcept;  // MIME тела
const char* submitExtension(SubmitFormat format) noexcept;    // ".fdf", ".xfdf", ".txt", ".pdf"
SubmitFormat submitFormat(int flags) noexcept;

enum class SubmitTarget { Mail, Http, Https, Unsupported };

// mailto:a@b,c@d?subject=…&cc=…&bcc=…&body=… (RFC 6068, %-кодирование UTF-8).
struct MailTo {
    std::vector<std::string> to;
    std::vector<std::string> cc;
    std::vector<std::string> bcc;
    std::string subject;
    std::string body;
};
std::optional<MailTo> parseMailto(std::string_view url);

struct SubmitPlan {
    std::string url;  // как в /F (GET — с запросом)
    SubmitTarget target = SubmitTarget::Unsupported;
    SubmitFormat format = SubmitFormat::Fdf;
    std::string contentType;
    bool get = false;
    std::vector<FieldValue> fields;  // отправляемые значения
    std::string body;                // FDF, XFDF, HTML; у PDF — пусто (собирает служба)
    std::string attachmentName;      // имя вложения письма: <документ><расширение формата>
    std::optional<MailTo> mail;
    int flags = 0;
};

// План отправки для действия action документа (documentFile — имя файла документа: /F в FDF/XFDF и имя
// вложения). Поля: /Fields (имена и ветки) с «исключить» — иначе все; без IncludeNoValueFields — только со
// значением.
SubmitPlan planSubmit(const Document& doc, const FieldAction& action, const std::string& documentFile);
// «имя=значение&…» (application/x-www-form-urlencoded, UTF-8; несколько значений — повтором имени).
std::string urlEncodeForm(const std::vector<FieldValue>& fields);
// Ответ сервера — FDF для импорта.
bool isFdfResponse(std::string_view contentType, std::string_view body);

}  // namespace ot::forms
