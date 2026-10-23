// Объекты PDF как текст — «qpdf --json в миниатюре» для сравнения документов в тестах журнала и сохранения:
// номер живого объекта → значение (pdf_print_obj, tight) и сырые байты потока; запись 0 — ссылки трейлера
// /Root, /Info, /Encrypt. Свободные записи xref (новые объекты после undo, хвост таблицы) не попадают — как в
// qpdf --json.
#pragma once

#include <map>
#include <string>

extern "C" {
#include <mupdf/pdf.h>
}

namespace ot {
class Document;
}  // namespace ot

namespace ot::test {

using PdfObjects = std::map<int, std::string>;

// Объекты открытого документа (с несохранёнными правками).
PdfObjects pdfObjects(fz_context* ctx, pdf_document* pdf);
PdfObjects pdfObjects(const Document& doc);
// Объекты PDF из байтов (открыть в памяти).
PdfObjects pdfObjectsOfBytes(const std::string& bytes);

// Полная запись документа в память (pdf_write_document, без сжатия и сборки мусора — номера объектов
// сохраняются) и её объекты: «байты после полной записи». История undo документа после неё сброшена.
PdfObjects pdfObjectsAfterFullWrite(Document& doc);

// Первое различие двух наборов объектов («12: …» или пусто) — для сообщения теста.
std::string firstDifference(const PdfObjects& a, const PdfObjects& b);

}  // namespace ot::test
