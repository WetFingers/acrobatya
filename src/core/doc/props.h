// Свойства документа (эталон 3.1 «Свойства документа», 3.13 «Метаданные»; view-45, view-46, ocr-32): /Info
// (чтение и запись, пользовательские ключи), синхронизация /Info → XMP (поток /Metadata: dc:title,
// dc:creator, dc:description, pdf:Keywords, pdf:Producer, xmp:CreatorTool, xmp:CreateDate, xmp:ModifyDate,
// xmp:MetadataDate, pdfx:<ключ>; нет XMP — создаётся, pdfaid и rdf:Bag не трогаются), /Lang каталога, Tagged
// (/MarkInfo /Marked), шрифты всех страниц (тип, встроен, подмножество, кодировка) с кешем по номеру правки,
// «Дополнительно» (версия, линеаризация, объекты, предупреждения pdf_check_document). Запись —
// Journal::Operation (undo); поток документа.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ot {

class Document;

namespace props {

struct InfoFields {
    // UTF-8; даты — строкой PDF ("D:20260101120000Z"); пустое поле при записи — ключ удаляется.
    std::string title, author, subject, keywords, creator, producer, creationDate, modDate;
    std::map<std::string, std::string> custom;  // прочие текстовые ключи /Info («Пользовательские»)
};

InfoFields readInfo(Document& doc);

struct WriteOptions {
    bool touchModDate = true;  // ModDate (и xmp:ModifyDate/MetadataDate) — время записи, а не info.modDate
};

// Записать /Info целиком (custom — полный набор пользовательских ключей) и синхронизировать XMP. Операция
// журнала «Document properties» (вложенная — внутри операции вызывающего). Не PDF — std::logic_error.
void writeInfo(Document& doc, const InfoFields& info, const WriteOptions& options = {});

// Пакет XMP из /Metadata каталога (UTF-8); нет — пусто.
std::string xmpPacket(Document& doc);

// /Lang каталога (ocr-32): основной язык документа ("ru-RU"); пусто — не задан / удалить.
std::string lang(Document& doc);
void setLang(Document& doc, const std::string& lang);

// «Документ с тегами»: /MarkInfo /Marked true.
bool tagged(Document& doc);

// /PageLayout каталога (имя: SinglePage, OneColumn, TwoColumnLeft…) — раскладка при открытии (эталон 3.1);
// нет или не PDF — пусто.
std::string pageLayout(Document& doc);

struct FontInfo {
    std::string name;      // /BaseFont (подмножество — с префиксом «ABCDEF+»)
    std::string type;      // Type1, MMType1, TrueType, Type3, «Type0 (CIDFontType2)»…
    std::string encoding;  // WinAnsiEncoding, Identity-H, «WinAnsiEncoding + Differences», «Built-in»…
    bool embedded = false;
    bool subset = false;
    int firstPage = 0;  // 0-based
    int pages = 0;      // на скольких страницах
};

// Шрифты ресурсов всех страниц (с формами XObject и внешним видом аннотаций), по порядку появления.
std::vector<FontInfo> fonts(Document& doc);

// Кеш списка шрифтов: пересчёт, только если документ или номер правки (DocumentSession::revision) другой.
class FontCache {
public:
    const std::vector<FontInfo>& fonts(Document& doc, std::uint64_t revision);
    int scans() const noexcept { return scans_; }
    void clear() noexcept { valid_ = false; }

private:
    const Document* doc_ = nullptr;
    std::uint64_t revision_ = 0;
    bool valid_ = false;
    int scans_ = 0;
    std::vector<FontInfo> list_;
};

struct Advanced {
    int pdfVersion = 0;  // 17 — PDF 1.7
    bool linearized = false;
    bool tagged = false;
    bool repaired = false;  // файл чинили (при открытии или проверкой)
    std::string pdfa;       // "PDF/A-2B" или пусто
    std::string lang;
    std::uintmax_t fileSize = 0;
    int objects = 0;  // живых объектов
    int pages = 0;
    std::vector<std::string> warnings;  // предупреждения и ошибки MuPDF при pdf_check_document
};

// «Дополнительно»; check — прогнать pdf_check_document (загружает все объекты; ремонт сбрасывает историю
// undo).
Advanced advanced(Document& doc, bool check = true);

}  // namespace props
}  // namespace ot
