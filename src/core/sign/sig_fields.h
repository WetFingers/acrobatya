// Поля подписи документа для интерфейса (эталон 3.8 «Инструмент», «Панель «Подписи»»; план 4.6, п. 1–2;
// forms-17 — поле /Sig, sign-12 — щелчок по пустому полю, sign-26 — панель перечисляет и пустые поля): все
// терминальные поля /FT /Sig дерева /AcroForm /Fields — подписанные (/V — словарь) и пустые; страница и рамка
// виджета (сам узел или первый потомок) в координатах страницы MuPDF, как у PageView и SignaturePlacement;
// только чтение (Ff бит 1: блокировка прежней подписью — FieldMDP, или автор формы), видимость (/Rect не
// пуст, нет флагов Hidden/NoView), метка времени документа (/Type /DocTimeStamp или /SubFilter
// /ETSI.RFC3161). Только чтение документа. Поток — поток документа.
#pragma once

#include <string>
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

}  // namespace ot::sign
