// Права изменений подписанного документа — чтение (эталон 3.8 «Валидатор», «Граничные случаи»; план 4.4, п.
// 4; sign-16 — проверка): DocMDP сертифицирующей подписи — /Root /Perms /DocMDP → её словарь /V, /Reference с
// /TransformMethod /DocMDP и /TransformParams /P (1 — никаких изменений, 2 — заполнение форм и подписи, 3 — и
// комментарии; нет /P — 2, ISO 32000-1 12.8.2.2); FieldMDP подписи — /Reference /FieldMDP и /Lock её поля
// (/Action /All | /Include | /Exclude, /Fields — полные имена). MuPDF P=2 и P=3 не различает
// (pdf_find_locked_fields) — поэтому свой разбор. Запись DocMDP — сессия 4.5. Поток — поток документа.
#pragma once

#include <string>
#include <string_view>
#include <vector>

extern "C" {
typedef struct fz_context fz_context;
typedef struct pdf_document pdf_document;
typedef struct pdf_obj pdf_obj;
}

namespace ot {
class Document;
}

namespace ot::sign {

// Разрешённые изменения (значения /P).
enum class MdpPermission {
    None = 0,         // DocMDP нет: подпись утверждающая — разрешено до комментариев включительно
    NoChanges = 1,    // P=1
    FormFilling = 2,  // P=2: заполнение форм, подписи, шаблоны страниц
    Annotations = 3,  // P=3: и комментарии
};

struct FieldLock {
    enum class Action { All, Include, Exclude };
    Action action = Action::All;
    std::vector<std::string> fields;  // полные имена (UTF-8)
};

struct DocMdp {
    MdpPermission permission = MdpPermission::None;
    std::string field;        // поле сертифицирующей подписи; пусто — нет
    int signatureObject = 0;  // номер объекта её /V
};

// DocMDP документа (/Perms /DocMDP); нет — permission None.
DocMdp readDocMdp(Document& doc);
DocMdp readDocMdp(fz_context* ctx, pdf_document* pdf);

// FieldMDP подписи: /Reference её /V с /TransformMethod /FieldMDP и /Lock поля.
std::vector<FieldLock> fieldLocks(fz_context* ctx, pdf_obj* field, pdf_obj* v);
// DocMDP в /Reference самой подписи (у сертифицирующей); нет — None.
MdpPermission signatureDocMdp(fz_context* ctx, pdf_obj* v);

// Поле заблокировано хоть одним правилом.
bool isFieldLocked(const std::vector<FieldLock>& locks, std::string_view fullName);

const char* mdpPermissionName(MdpPermission p);

}  // namespace ot::sign
