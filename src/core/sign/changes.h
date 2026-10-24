// Изменения после подписи — анализ ревизий (эталон 3.8 «Валидатор», 4.5; план 4.4, п. 4; sign-25 — список
// изменений для вердикта, sign-16 — проверка DocMDP/FieldMDP). Подписанная ревизия — байты [0, конец
// /ByteRange) во временном документе (той же семьи контекстов), текущая — сам документ; номера объектов
// инкрементальных обновлений общие. Кандидаты — объекты, определённые в секциях после подписанной (у
// восстановленного файла — все); каждый сравнивается с прежним значением (словари — по ключам, потоки — по
// сырым байтам), неизменённые переписанные копии изменениями не считаются.
//
// Роль объекта — по достижимости из каталога текущей (удалённого — прежней) ревизии, строжайшая из ролей:
// содержимое страниц и прочее (Other) > комментарии > формы > /DSS, /Info, /Metadata (LtvOnly); недостижимые
// (сироты, потоки xref и объектов) — не изменения. Контейнеры разбираются по ключам: каталог (/DSS,
// /Extensions, /Metadata, /Version — LTV; /AcroForm — формы; прочее — Other), страница (только /Annots:
// виджет — формы, комментарий — комментарии; остальное — Other), /AcroForm (/Fields: новое поле подписи —
// формы, другое поле — Other; /SigFlags /DR /NeedAppearances /DA /XFA — формы), поле (/V /AS /AP /MK, бит
// ReadOnly — формы, у заблокированного FieldMDP или при P=1 — «заблокированное поле изменено»; /V у
// подписанного поля — Other). Новое значение подписи — формы, метки документа — LTV; правка прежнего значения
// подписи — Other.
//
// Разрешено: DSS, VRI и метки документа — всегда (ETSI EN 319 142-1, 5.4.2.3 и 5.4.3); P=1 — больше ничего;
// P=2 — формы и подписи; P=3 и утверждающая подпись без DocMDP — и комментарии; Other — никогда (как
// pyHanko). Поток — поток документа.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/sign/docmdp.h"

namespace ot {
class Document;
}

namespace ot::sign {

enum class ChangeKind {
    Dss,             // /DSS, VRI, сертификаты, OCSP, CRL
    DocTimestamp,    // метка времени документа
    Metadata,        // /Info, XMP, /Extensions, /Version
    Signature,       // новая подпись (поле подписано)
    SignatureField,  // добавлено пустое поле подписи
    FormField,       // значение или вид поля формы
    LockedField,     // изменено заблокированное поле (FieldMDP, P=1)
    Annotation,      // комментарий добавлен, изменён или удалён
    Page,            // содержимое, ресурсы, рамки страницы; страницы добавлены или удалены
    FieldStructure,  // поле формы добавлено, удалено, переименовано
    Other,           // прочие объекты каталога (закладки, имена, структура, /Perms…)
};
const char* changeKindName(ChangeKind kind);

// Уровень изменений по возрастанию: что нужно разрешить, чтобы они были законны.
enum class ChangeLevel { None, LtvOnly, FormFilling, Annotations, Other };
const char* changeLevelName(ChangeLevel level);

struct Change {
    ChangeKind kind = ChangeKind::Other;
    int page = -1;       // страница (−1 — не о странице)
    int object = 0;      // номер объекта
    std::string field;   // поле формы или подписи (полное имя)
    std::string detail;  // английский — журнал и «Подробнее»
    bool allowed = false;
};

struct ChangeRules {
    MdpPermission docMdp = MdpPermission::None;  // DocMDP документа
    std::vector<FieldLock> locks;                // FieldMDP проверяемой подписи
};

struct ChangeReport {
    bool modified = false;  // после подписанной ревизии что-то изменилось
    ChangeLevel level = ChangeLevel::None;
    bool allowed = true;  // все изменения разрешены правилами
    std::vector<Change> changes;
};

// Уровень, разрешённый правилами (P=1 — LtvOnly, P=2 — FormFilling, P=3 и без DocMDP — Annotations).
ChangeLevel permittedLevel(MdpPermission docMdp);

// Изменения документа после ревизии, кончающейся на signedEnd (конец /ByteRange). Ошибка разбора —
// исключение.
ChangeReport analyzeChanges(Document& doc, std::int64_t signedEnd, const ChangeRules& rules);

}  // namespace ot::sign
