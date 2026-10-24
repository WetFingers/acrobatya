// Поля подписи документа для интерфейса (эталон 3.8 «Инструмент», «Панель «Подписи»»; план 4.6, п. 1–2;
// forms-17 — поле /Sig, sign-12 — щелчок по пустому полю, sign-26 — панель перечисляет и пустые поля): все
// терминальные поля /FT /Sig дерева /AcroForm /Fields — подписанные (/V — словарь) и пустые; страница и рамка
// виджета (сам узел или первый потомок) в координатах страницы MuPDF, как у PageView и SignaturePlacement;
// только чтение (Ff бит 1: блокировка прежней подписью — FieldMDP, или автор формы), видимость (/Rect не
// пуст, нет флагов Hidden/NoView), метка времени документа (/Type /DocTimeStamp или /SubFilter
// /ETSI.RFC3161). Только чтение документа. Поток — поток документа.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/util/geometry.h"

namespace ot {
class Document;
}

namespace ot::sign {

struct SignatureField {
    std::string name;  // полное имя поля (UTF-8)
    int page = -1;     // страница виджета (−1 — виджета на страницах нет)
    RectF rect;        // рамка виджета в координатах страницы MuPDF
    bool visible = false;
    bool isSigned = false;
    bool readOnly = false;
    bool docTimestamp = false;
};

// Поля подписи в порядке обхода /AcroForm /Fields. Не PDF — пусто.
std::vector<SignatureField> signatureFields(Document& doc);

// Пустое поле подписи под точкой страницы (координаты MuPDF), которое можно подписать (видимое, не только
// чтение); nullptr — нет.
const SignatureField* emptyFieldAt(const std::vector<SignatureField>& fields, int page, PointF point);

// «Очистить подпись» (план 4.6, п. 2; эталон 3.8 «Панель «Подписи»»; sign-22): можно ли. Нельзя — поле
// только для чтения (заблокировано своей или чужой подписью) или после этой подписи в файле есть другие
// (очистка — изменение, которое сделало бы их недействительными).
enum class ClearBlock { None, NotFound, NotSigned, ReadOnly, LaterSignatures };
ClearBlock clearBlock(Document& doc, std::string_view field);
// Очистить подпись поля одной операцией журнала «Clear signature»: pdf_clear_signature (/V снят, вид — пустое
// поле MuPDF), у сертифицирующей — и /Perms /DocMDP каталога. Записать — Saver (инкремент: прежняя ревизия с
// подписью остаётся в файле). Нельзя (clearBlock) — std::runtime_error.
void clearSignature(Document& doc, std::string_view field);

}  // namespace ot::sign
