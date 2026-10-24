// Внутреннее ядра аннотаций (только src/core/annot/*.cpp): страница с картой «номер объекта → pdf_annot» на
// время одного вызова, перенос геометрии словаря аннотации матрицей, общие ошибки. Указатели MuPDF живут
// только внутри вызова функции ядра — наружу не уходят (эталон 4.1).
#pragma once

#include <string>
#include <unordered_map>

#include "core/annot/annots.h"
#include "core/mu/handles.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace ot::annot::detail {

pdf_document* pdfOf(const Document& doc);  // не PDF — std::logic_error
[[noreturn]] void notFound(const AnnotRef& ref);
[[noreturn]] void badArgument(const std::string& message);

// Страница на время вызова: pdf_page со списками аннотаций и виджетов, /Annots, матрица страницы
// (pdf_page_transform: пункты PDF → координаты страницы MuPDF) и карта номеров объектов.
class PageAnnots {
public:
    PageAnnots(const Document& doc, int index);
    PageAnnots(const PageAnnots&) = delete;
    PageAnnots& operator=(const PageAnnots&) = delete;

    fz_context* ctx() const noexcept { return ctx_; }
    pdf_document* pdf() const noexcept { return pdf_; }
    pdf_page* page() const noexcept { return page_; }
    pdf_obj* annots() const noexcept { return annots_; }  // может быть nullptr
    fz_matrix ctm() const noexcept { return ctm_; }
    int index() const noexcept { return index_; }

    // pdf_annot из списков MuPDF (аннотации и виджеты); Link, Popup и прочие — nullptr.
    pdf_annot* find(int objnum) const;
    // Место в /Annots; нет — −1.
    int indexOf(int objnum) const;
    // Словарь записи /Annots по номеру; нет — nullptr.
    pdf_obj* object(int objnum) const;
    // Перечитать списки и /Annots после создания или удаления (pdf_sync_annots — у правки в обход MuPDF).
    void refresh(bool syncAnnots = false);
    // /Annots есть (создать пустой массив).
    pdf_obj* ensureAnnots();

private:
    fz_context* ctx_;
    pdf_document* pdf_;
    mu::PageHandle handle_;
    pdf_page* page_ = nullptr;
    pdf_obj* annots_ = nullptr;
    fz_matrix ctm_ = fz_identity;
    int index_ = 0;
    std::unordered_map<int, pdf_annot*> byNum_;
};

// Перенести геометрию словаря аннотации матрицей m в пунктах PDF: /Rect, /QuadPoints, /InkList, /Vertices,
// /L, /CL и /Rect её /Popup. Вызывать внутри mu::call.
void transformGeometry(fz_context* ctx, pdf_obj* annot, fz_matrix m);

// Матрица в пунктах PDF, которая в координатах страницы (ctm) даёт преобразование pageSpace.
fz_matrix rawMatrix(fz_matrix ctm, fz_matrix pageSpace);

// Типы, внешний вид которых MuPDF синтезирует заново после смены размера (линии, фигуры, Ink, FreeText,
// разметка, каретка); значки и штампы вписываются в рамку.
bool resynthesizeOnResize(AnnotType type) noexcept;

// Секунды Unix «сейчас».
std::int64_t now();

}  // namespace ot::annot::detail
