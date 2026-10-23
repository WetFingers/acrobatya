// Вложения документа (план 2.4; эталон 3.1 «Навигация», 7.3; view-17, security-43): файлы дерева
// /Names /EmbeddedFiles и аннотаций /FileAttachment всех страниц — список (имя, описание, размер, даты, тип,
// где лежит) и содержимое. Открыть внешней программой можно не всё: расширения чёрного списка эталона 7.3
// (exe, bat, cmd, com, js, jse, vbs, vbe, ps1, msi, msp, scr, pif, reg, hta, jar, lnk, wsf, wsh, cpl, inf) —
// только «Сохранить как…» (isBlocked). Имя из файла недоверенное: перед записью на диск — safeFileName (без
// каталогов, зарезервированных символов и имён устройств Windows). Добавление и удаление — сессия 7.5.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ot {

class Document;

namespace attachments {

enum class Source {
    Document,    // дерево /Names /EmbeddedFiles
    Annotation,  // аннотация /FileAttachment
};

struct Attachment {
    Source source = Source::Document;
    std::string key;             // ключ дерева /EmbeddedFiles (Document)
    int page = -1;               // страница аннотации (Annotation)
    int objnum = 0;              // номер объекта аннотации (Annotation)
    std::string fileName;        // /UF, иначе /F (UTF-8, как в файле — недоверенное)
    std::string description;     // /Desc (или /Contents аннотации)
    std::string mimeType;        // /Subtype потока /EF, если есть
    std::int64_t size = -1;      // /Params /Size; −1 — неизвестен
    std::int64_t created = -1;   // секунды с 1970 (UTC); −1 — нет
    std::int64_t modified = -1;  // то же
    bool embedded = true;        // false — спецификация без /EF (внешний файл: открыть нечего)
};

// Все вложения: сначала дерево /EmbeddedFiles (по порядку ключей), затем аннотации по страницам. Не PDF —
// пусто.
std::vector<Attachment> list(Document& doc);

// Содержимое вложения (раскодированный поток /EF). Нет такого или не встроено — MuError.
std::string contents(Document& doc, const Attachment& attachment);

// Расширение имени в нижнем регистре без точки («Report.DOCX» → «docx»; хвостовые точки и пробелы, которые
// Windows отбрасывает, не считаются: «a.exe. » → «exe»); нет — пусто.
std::string extensionOf(std::string_view fileName);
// Расширение из чёрного списка эталона 7.3 — открывать нельзя, только сохранить.
bool isBlocked(std::string_view fileName);
bool isPdf(std::string_view fileName);
// Имя для записи на диск: последний компонент пути, без управляющих и <>:"/\|?*, без хвостовых точек и
// пробелов, имя устройства (CON, NUL, COM1…) — с «_» впереди; пусто — «attachment».
std::string safeFileName(std::string_view fileName);

}  // namespace attachments
}  // namespace ot
