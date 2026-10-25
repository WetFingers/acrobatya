// Внутреннее ядра аннотаций (только src/core/annot/*.cpp): страница с картой «номер объекта → pdf_annot» на
// время одного вызова, перенос геометрии словаря аннотации матрицей, общие ошибки. Указатели MuPDF живут
// только внутри вызова функции ядра — наружу не уходят (эталон 4.1).
#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

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

// Создать одну аннотацию по props внутри операции вызывающего (annot_edit.cpp): irt > 0 — /IRT на этот
// объект и /RT rt; autoPopup = false — у записки без окна /Popup (ответы и статусы 5.4). Вне mu::call.
AnnotRef createIn(Document& doc, PageAnnots& pa, const AnnotProps& props, int irt, const char* rt,
                  bool autoPopup = true);

// Секунды Unix «сейчас».
std::int64_t now();

// Поле FreeText в пунктах PDF: /Rect минус /RD по ISO 32000 (левый, верхний, правый, нижний; у MuPDF второй —
// нижний). Внутри mu::call.
fz_rect freeTextDesign(fz_context* ctx, pdf_obj* annot);
// Внешний вид FreeText своим шрифтом /DA (freetext.h): внутри операции вызывающего, вне mu::call; pa —
// страница аннотации (после refresh). Шрифт не встроился (нет файла, запрет встраивания) — синтез MuPDF.
void writeFreeText(Document& doc, PageAnnots& pa, int objnum);
// Вид заново: FreeText — writeFreeText (без dirty — вид прежний), прочие — pdf_update_annot (dirty — сначала
// pdf_dirty_annot: синтез обязателен). Вне mu::call.
void updateAppearance(Document& doc, PageAnnots& pa, int objnum, bool dirty);

// Шрифты base14 вида /AP /N (синтез MuPDF: штамп, подпись линии-измерения /Cap) — дописать /Widths и
// /FontDescriptor, которых требуют PDF 2.0 и строгий pdfcpu. Вне mu::call.
void completeBase14Fonts(fz_context* ctx, pdf_document* pdf, pdf_obj* annot);

// ---------------------------------------------------------------- обмен FDF/XFDF (5.4, annot_exchange.cpp)

// Набор записи: refs пусто — все аннотации, кроме окон /Popup и полей форм; иначе — refs и их ветки
// (comments.h branch). По страницам и номерам объектов.
std::vector<AnnotRef> exchangeSet(Document& doc, const std::vector<AnnotRef>& refs);
// Номер объекта аннотации страницы с /NM nm (не окна); 0 — нет. Вне mu::call.
int findByName(PageAnnots& pa, const std::string& nm);
// Перенесённая аннотация ref (косвенный объект документа) — на страницу: /P и в /Annots; та же /NM уже есть —
// на её место (её окно уходит). Внутри операции вызывающего, вне mu::call. true — заменила прежнюю.
bool placeImported(PageAnnots& pa, pdf_obj* ref, const std::string& nm);
// Окно записки popup (косвенный словарь) — /Parent parent, /P, в /Annots; у родителя — /Popup. Вне mu::call.
void attachPopup(PageAnnots& pa, pdf_obj* parent, pdf_obj* popup);
// После импорта (в операции вызывающего): списки аннотаций страниц заново, вид у перенесённых без /AP —
// синтез MuPDF (FreeText — свой шрифт).
void finishImported(Document& doc, const std::map<int, std::vector<int>>& byPage);

}  // namespace ot::annot::detail
