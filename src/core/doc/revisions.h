// Ревизии PDF и подписанная версия (эталон 4.5, sign-28 — ядро; валидатор — 4.4, панель «Подписи» — 4.6):
// число версий файла (pdf_count_versions: 1 + инкрементальные обновления на диске), проверка истории
// изменений после подписей (pdf_validate_change_history), подписи с концом /ByteRange и «Просмотреть
// подписанную версию» — байты [0, конец диапазона) подписи во временный файл (вкладка «только чтение» —
// интерфейс 4.6). Байты берутся из потока документа (файл или память), несохранённые правки на них не влияют.
// Поток — поток документа.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ot {

class Document;

namespace revisions {

struct SignatureInfo {
    int index = 0;                 // по порядку обхода /AcroForm /Fields (только подписанные поля)
    std::string field;             // полное имя поля (UTF-8)
    std::int64_t rangeEnd = 0;     // конец /ByteRange — конец подписанной ревизии
    bool coversWholeFile = false;  // после подписи в файл ничего не дописано
    int version = 0;               // ревизия файла с подписью: 1 — первая (исходная запись)
    // pdf_validate_signature: 0 — изменения после подписи её не задевают; n — задели n сохранений назад; −1 —
    // в несохранённых правках. Строго, как pyHanko: правка страницы с виджетом подписи (записка в её /Annots)
    // — уже изменение; «разрешено ли» решает валидатор (4.4).
    int changedSince = 0;
};

// Версий файла (1 — без инкрементальных обновлений). Не PDF — 0.
int versionCount(Document& doc);
// pdf_validate_change_history: 0 — изменения после подписей не нарушают их прав (DocMDP, /Lock — FieldMDP);
// n > 0 — первая нарушившая версия (n сохранений назад). У утверждающей подписи без прав допустимо всё —
// посмотреть, задета ли сама подпись, — SignatureInfo::changedSince.
int lastValidVersion(Document& doc);
inline bool changeHistoryValid(Document& doc) { return lastValidVersion(doc) == 0; }

// Подписанные поля документа.
std::vector<SignatureInfo> signatures(Document& doc);

// Байты подписанной ревизии [0, rangeEnd) подписи sigIndex. Нет такой подписи — std::out_of_range.
std::string signedRevision(Document& doc, int sigIndex);
// То же во временный файл (dir; пусто — %TEMP%\AcroBatya): «<имя>.signed-<N>.pdf». Удаляет вызывающий.
std::filesystem::path signedRevisionBytes(Document& doc, int sigIndex, const std::filesystem::path& dir = {});

}  // namespace revisions
}  // namespace ot
