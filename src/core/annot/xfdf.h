// Обмен комментариями XFDF (план 5.4; эталон 3.4 «FDF/XFDF»; comment-27): модель без XML — XML пишет и читает
// services/xfdf_xml.h (QXmlStreamWriter/Reader, пространство имён http://ns.adobe.com/xfdf/). Атрибуты — как
// у Adobe (XFDF 3.0): page (с 0), rect «x0,y0,x1,y1» и прочая геометрия — в пунктах PDF (пространство
// страницы, начало внизу слева, без /Rotate), color/interior-color «#RRGGBB», flags «print,nozoom,…», name
// (/NM), title
// (/T), subject, date и creationdate (строки дат PDF как в файле), opacity, width, style, dashes, intent,
// icon, coords (/QuadPoints), start/end/head/tail (линия), callout/justification (FreeText), inreplyto (/NM
// родителя), replyType (reply|group), state, statemodel, file/mimetype (вложение); дети — <contents>,
// <contents-richtext> (XHTML /RC), <defaultappearance> (/DA), <defaultstyle> (/DS), <vertices>,
// <inklist><gesture>, <popup rect open flags>, <data> (вложение, base64).
//
// Подтипы — элементы по имени /Subtype строчными (text, highlight, freetext…, «3D» — threed); окно /Popup —
// ребёнком родителя; поля форм (Widget) — не комментарии (данные форм — 6.1) и в XFDF аннотаций не идут.
// Импорт — как FDF (core/annot/fdf.h): совмещение по /NM, страница вне документа — пропуск с отчётом, <f
// href> на другой файл — отказ; одна операция «Import comments»; вид у новых — синтез MuPDF (FreeText — свой
// шрифт).
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/annot/annots.h"
#include "core/annot/fdf.h"

namespace ot {

class Document;

namespace annot {

inline constexpr std::string_view kXfdfNamespace = "http://ns.adobe.com/xfdf/";

struct XfdfAnnot {
    std::string element;  // "text", "highlight"…
    std::vector<std::pair<std::string, std::string>> attributes;
    std::string contents;
    std::string richText;  // XHTML /RC целиком
    std::string defaultAppearance;
    std::string defaultStyle;
    std::vector<std::vector<PointF>> ink;  // <inklist><gesture>
    std::vector<PointF> vertices;          // <vertices>
    bool hasPopup = false;
    RectF popupRect;  // пункты PDF
    bool popupOpen = false;
    int popupFlags = 0;
    // Вложение (fileattachment): имя — атрибут file, содержимое — <data> (base64 в XML).
    std::string fileData;
    bool hasFileData = false;

    // Значение атрибута; нет — пусто.
    std::string attribute(std::string_view name) const;
    bool has(std::string_view name) const;
    void set(std::string name, std::string value);
};

struct XfdfDocument {
    std::string file;  // <f href>
    std::vector<XfdfAnnot> annots;
};

// Имя элемента по подтипу ("text"; 3D — "threed"; Popup и Widget — пусто: не элементы) и обратно (незнакомое
// — Unknown).
std::string xfdfElement(AnnotType type);
AnnotType xfdfType(std::string_view element);

// Модель XFDF документа: набор — как exportFdf (refs пусто — все, кроме окон и полей форм).
XfdfDocument exportXfdf(Document& doc, const std::vector<AnnotRef>& refs, const std::string& fileName);
// Что в модели — для подтверждения импорта.
ExchangeInfo probeXfdf(const XfdfDocument& xfdf);
// Перенести в документ (одна операция «Import comments»); documentFile — для проверки <f href>.
ImportReport importXfdf(Document& doc, const XfdfDocument& xfdf, const std::string& documentFile);

// Числа атрибутов: «x,y,x,y…» и «x,y;x,y» (разделители — запятая, точка с запятой, пробел).
std::vector<float> parseNumbers(std::string_view text);
std::string formatNumbers(const std::vector<float>& values, char separator = ',');

}  // namespace annot
}  // namespace ot
