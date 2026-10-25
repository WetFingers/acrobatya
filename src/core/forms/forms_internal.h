// Внутреннее ядра форм (только src/core/forms/*.cpp): страницы с виджетами на время правки (MuPDF обновляет
// вид только у открытых страниц — pdf_update_page обходит их все), свой вид текстового поля (кириллица —
// встроенный TrueType, пароль — звёздочки). Указатели MuPDF живут только внутри вызова функции ядра.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/mu/handles.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace ot {
class Document;
}

namespace ot::forms::detail {

pdf_document* pdfOf(const Document& doc);  // не PDF — std::logic_error

// Страницы документа, на которых есть виджеты (по /Annots без загрузки), открытые на время правки.
class WidgetPages {
public:
    explicit WidgetPages(Document& doc);
    WidgetPages(const WidgetPages&) = delete;
    WidgetPages& operator=(const WidgetPages&) = delete;

    fz_context* ctx() const noexcept { return ctx_; }
    pdf_document* pdf() const noexcept { return pdf_; }
    // Виджет по номеру объекта и его страница; нет — {nullptr, −1}.
    std::pair<pdf_annot*, int> find(int objnum) const;
    // Обновить вид (pdf_update_page — пересчёт при doc->recalculate и синтез всех открытых страниц) и собрать
    // изменившиеся виджеты; затем свой вид текстовым полям, которым он нужен. Страницы с изменившимся видом —
    // по возрастанию. Внутри операции вызывающего.
    std::vector<int> update();

private:
    Document& doc_;
    fz_context* ctx_;
    pdf_document* pdf_;
    std::vector<std::pair<int, mu::PageHandle>> pages_;
};

// Свой вид текстового виджета, если он нужен: шрифт /DA — наш встроенный (core/annot/freetext.h FieldFont),
// или в значении есть знаки вне WinAnsi, которые есть в шрифте семейства /DA (тогда /DA меняется на
// встроенный), или поле пароля (звёздочки). Значение — формат /AA /F при включённом JavaScript. true — вид
// записан. Внутри операции вызывающего, вне mu::call.
bool writeOwnTextAppearance(Document& doc, pdf_annot* widget);

// Текст объекта (строка, имя, поток) в UTF-8; nullptr — пусто. Вне mu::call.
std::string textOf(fz_context* ctx, pdf_obj* obj);

}  // namespace ot::forms::detail
