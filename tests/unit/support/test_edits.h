// Правки PDF для тестов журнала и сохранения: каталог, /Rotate, десять разных правок, удаление страницы
// (тяжёлая), записка. Каждая правка, кроме putRootName/setRotate, открывает свою Journal::Operation;
// putRootName/setRotate — внутри операции вызывающего.
#pragma once

#include <string>

extern "C" {
#include <mupdf/pdf.h>
}

namespace ot {
class Document;
}

namespace ot::test {

fz_context* ctxOf(const Document& doc);
pdf_document* pdfOf(const Document& doc);

// Имя в каталоге (/Root): записать (внутри операции) и прочитать.
void putRootName(Document& doc, const char* key, const char* value);
std::string rootName(const Document& doc, const char* key);
// /Rotate страницы (внутри операции).
void setRotate(Document& doc, int page, int rotate);
// Шагов в журнале MuPDF (pdf_undoredo_state).
int undoSteps(const Document& doc);

// Десять разных правок, каждая — своя операция: /Rotate, /MediaBox, /PageMode, /PageLayout, поток
// содержимого, картинка, записка, вставка и удаление страницы, новый объект. Документ из 3+ страниц.
void tenEdits(Document& doc);
// Удалить страницу (тяжёлая правка, все страницы).
void deletePage(Document& doc, int page);
// Записка (Text) на странице.
void addNote(Document& doc, int page, const char* text);

}  // namespace ot::test
