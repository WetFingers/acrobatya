// Обмен комментариями FDF (план 5.4; эталон 3.4 «FDF/XFDF», 7.3; comment-27, security-36). Запись — свой
// сериализатор: пустой документ MuPDF (pdf_create_document), в каталоге /FDF << /Annots [...] /F (имя) >>,
// аннотации — копии словарей (pdf_graft_mapped_object: внешний вид с ресурсами, вложения — вместе) с /Page N
// вместо /P, /IRT — на копию родителя, окна /Popup — отдельными записями с /Parent; заголовок «%FDF-1.2».
// Чтение — pdf_open_document (MuPDF читает FDF восстановлением) → перенос обратно: /Page → страница, /IRT —
// по перенесённым или по /NM на странице, окна — к своим родителям; та же /NM на странице — запись
// обновляется (встаёт на место прежней), иначе — новая; страница вне документа — пропуск с отчётом. /F на
// другой файл — отказ (ExchangeError::ForeignDocument): чужие комментарии в свой документ не подмешиваются.
//
// Набор записи: refs пусто — все аннотации документа, кроме окон (идут со своими) и полей форм (данные форм —
// 6.1); иначе — refs с их ветками (ответы, статусы, части групп — core/annot/comments.h). Импорт — одна
// операция журнала «Import comments» (undo убирает всё); у перенесённых без /AP вид синтезируется в ней же.
#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "core/annot/annots.h"

namespace ot {

class Document;

namespace annot {

class ExchangeError : public std::runtime_error {
public:
    enum class Kind { Malformed, ForeignDocument };
    ExchangeError(Kind kind, const std::string& message, std::string file = {})
        : std::runtime_error(message), kind_(kind), file_(std::move(file)) {}
    Kind kind() const noexcept { return kind_; }
    // ForeignDocument — имя документа из файла обмена.
    const std::string& file() const noexcept { return file_; }

private:
    Kind kind_;
    std::string file_;
};

// Итог импорта FDF/XFDF.
struct ImportReport {
    int created = 0;
    int updated = 0;                // та же /NM уже была на странице
    int skipped = 0;                // страница вне документа
    std::vector<int> skippedPages;  // номера страниц файла (с 0) вне документа — по возрастанию, без повторов
    std::vector<AnnotRef> refs;     // созданные и обновлённые
    int imported() const noexcept { return created + updated; }
};

// Что в файле обмена — для подтверждения (эталон 7.3): имя документа (/F; пусто — нет), число аннотаций (без
// окон /Popup) и наибольший номер страницы (−1 — нет).
struct ExchangeInfo {
    std::string file;
    int annots = 0;
    int maxPage = -1;
};

// Документ назначения тот же, что в файле обмена: имена без пути, без учёта регистра; пустое — да.
bool sameDocumentName(std::string_view exchangeFile, std::string_view documentFile);

// Записать FDF (байты). fileName — /F (имя PDF без пути). Не PDF — std::logic_error.
std::string exportFdf(Document& doc, const std::vector<AnnotRef>& refs, const std::string& fileName);
// Разобрать FDF: не FDF/повреждён — ExchangeError Malformed.
ExchangeInfo probeFdf(std::string_view fdf);
// Перенести аннотации FDF в документ (одна операция «Import comments»). documentFile — имя файла документа
// для проверки /F (пусто — без проверки).
ImportReport importFdf(Document& doc, std::string_view fdf, const std::string& documentFile);

}  // namespace annot
}  // namespace ot
